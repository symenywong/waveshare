import { z } from 'zod'

import {
  type PairingCryptoProvider,
  type PairingCryptoSession,
} from './pairingCryptoProvider'
import {
  ManagementFrameDecoder,
  encodeManagementFrame,
  type ManagementFrame,
} from './serialFrameCodec'
import type {
  SerialApi,
  SerialPort,
  SerialReader,
  SerialWriter,
} from './serialHelloClient'
import { setSerialPortIdle } from './serialHelloClient'
import { helloDiagnosticsSchema } from './serialDiagnostics'

const encoder = new TextEncoder()
const decoder = new TextDecoder('utf-8', { fatal: true })
const REQUEST_TIMEOUT_MS = 12_000

const helloResultSchema = z
  .object({
    protocol: z.literal('aiqa-management'),
    version: z.literal(1),
    maxFrameBytes: z.literal(4096),
    authentication: z.literal('authentication_required'),
    diagnostics: helloDiagnosticsSchema.optional().default(null),
  })
  .strict()

const prepareResultSchema = z
  .object({
    credentialId: z.number().int().positive().max(0xffff_ffff),
    handshakeId: z.string().regex(/^[0-9a-f]{16}$/),
    deviceId: z.string().regex(/^(?:[0-9a-f]{2}){1,32}$/),
    deviceNonce: z.string().regex(/^[0-9a-f]{64}$/),
  })
  .strict()

const binaryResultSchema = (field: string, bytesMax: number) =>
  z
    .object({ [field]: z.string().regex(new RegExp(`^(?:[0-9a-f]{2}){1,${bytesMax}}$`)) })
    .strict()

interface Envelope {
  readonly v: 1
  readonly id: number
  readonly ok: boolean
  readonly result?: unknown
  readonly error?: { readonly code?: unknown }
}

function bytesToHex(value: Uint8Array): string {
  return Array.from(value, (byte) => byte.toString(16).padStart(2, '0')).join('')
}

function hexToBytes(value: string): Uint8Array {
  if (value.length % 2 !== 0 || !/^[0-9a-f]+$/.test(value)) {
    throw new Error('设备配对响应无效')
  }
  const output = new Uint8Array(value.length / 2)
  for (let index = 0; index < output.length; index += 1) {
    output[index] = Number.parseInt(value.slice(index * 2, index * 2 + 2), 16)
  }
  return output
}

function parseEnvelope(payload: Uint8Array, expectedId: number): Envelope {
  try {
    const value: unknown = JSON.parse(decoder.decode(payload))
    if (
      typeof value !== 'object' ||
      value === null ||
      !('v' in value) ||
      value.v !== 1 ||
      !('id' in value) ||
      value.id !== expectedId ||
      !('ok' in value) ||
      typeof value.ok !== 'boolean'
    ) {
      throw new Error()
    }
    return value as Envelope
  } catch {
    throw new Error('设备管理响应无效')
  }
}

export class DeviceManagementError extends Error {
  constructor(readonly code: string) {
    super(code)
    this.name = 'DeviceManagementError'
  }
}

function resultFromEnvelope(envelope: Envelope): unknown {
  if (envelope.ok) return envelope.result
  const code = envelope.error?.code
  throw new DeviceManagementError(typeof code === 'string' ? code : 'INTERNAL_ERROR')
}

async function withTimeout<T>(operation: Promise<T>, timeoutMs = REQUEST_TIMEOUT_MS): Promise<T> {
  let timer: ReturnType<typeof setTimeout> | undefined
  try {
    return await Promise.race([
      operation,
      new Promise<never>((_resolve, reject) => {
        timer = setTimeout(() => reject(new Error('设备响应超时')), timeoutMs)
      }),
    ])
  } finally {
    if (timer !== undefined) clearTimeout(timer)
  }
}

export class SerialManagementConnection {
  private readonly frames = new ManagementFrameDecoder()
  private readonly pendingFrames: ManagementFrame[] = []
  private nextRequestId = 1
  private cryptoSession: PairingCryptoSession | null = null
  private closed = false
  private busy = false

  get isClosed(): boolean {
    return this.closed
  }

  private constructor(
    private readonly port: SerialPort,
    private readonly reader: SerialReader,
    private readonly writer: SerialWriter,
  ) {}

  static async connect(serial: SerialApi): Promise<SerialManagementConnection> {
    let port: SerialPort
    try {
      port = await serial.requestPort()
    } catch {
      throw new Error('设备连接失败')
    }
    return SerialManagementConnection.connectPort(port)
  }

  static async connectPort(port: SerialPort): Promise<SerialManagementConnection> {
    let connection: SerialManagementConnection | null = null
    try {
      await port.open({ baudRate: 115200 })
      await setSerialPortIdle(port)
      if (port.readable === null || port.writable === null) throw new Error()
      connection = new SerialManagementConnection(
        port,
        port.readable.getReader(),
        port.writable.getWriter(),
      )
      const hello = helloResultSchema.parse(
        await connection.requestPublic('system.hello'),
      )
      if (hello.authentication !== 'authentication_required') throw new Error()
      return connection
    } catch {
      if (connection !== null) await connection.close()
      else await SerialManagementConnection.closePortBestEffort(port)
      throw new Error('设备连接失败')
    }
  }

  private static async closePortBestEffort(port: SerialPort): Promise<void> {
    try {
      await withTimeout(setSerialPortIdle(port), 1_000)
    } catch {
      // Continue closing even if the browser cannot release control lines.
    }
    try {
      await withTimeout(port.close(), 1_000)
    } catch {
      // A failed connection never exposes device-provided details.
    }
  }

  private ensureOpen(): void {
    if (this.closed) throw new Error('设备连接已关闭')
  }

  private async readFrame(): Promise<ManagementFrame> {
    const queued = this.pendingFrames.shift()
    if (queued !== undefined) return queued
    for (;;) {
      const result = await withTimeout(this.reader.read())
      if (result.done) throw new Error('设备连接已断开')
      if (result.value === undefined) continue
      const frames = this.frames.push(result.value)
      const first = frames[0]
      if (first === undefined) continue
      this.pendingFrames.push(...frames.slice(1))
      return first
    }
  }

  private async serialize<T>(operation: () => Promise<T>): Promise<T> {
    this.ensureOpen()
    if (this.busy) throw new Error('设备正在处理上一项操作')
    this.busy = true
    try {
      return await operation()
    } catch (error) {
      if (!(error instanceof DeviceManagementError)) await this.close()
      throw error
    } finally {
      this.busy = false
    }
  }

  private requestPayload(method: string, params?: unknown): {
    readonly id: number
    readonly payload: Uint8Array
  } {
    const id = this.nextRequestId
    this.nextRequestId = id === 0xffff_ffff ? 1 : id + 1
    return {
      id,
      payload: encoder.encode(
        JSON.stringify(params === undefined ? { id, method } : { id, method, params }),
      ),
    }
  }

  async requestPublic(method: string, params?: unknown): Promise<unknown> {
    return this.serialize(async () => {
      const request = this.requestPayload(method, params)
      await withTimeout(
        this.writer.write(encodeManagementFrame('request', request.payload)),
      )
      const frame = await this.readFrame()
      if (frame.kind !== 'response') throw new Error('设备管理响应无效')
      return resultFromEnvelope(parseEnvelope(frame.payload, request.id))
    })
  }

  async pair(pairingCode: string, provider: PairingCryptoProvider): Promise<void> {
    if (!/^\d{8}$/.test(pairingCode)) throw new Error('请输入设备屏幕上的 8 位配对码')
    const clientNonce = crypto.getRandomValues(new Uint8Array(32))
    let session: PairingCryptoSession | null = null
    let prepareCompleted = false
    try {
      const prepared = prepareResultSchema.parse(
        await this.requestPublic('pairing.prepare', {
          clientNonce: bytesToHex(clientNonce),
        }),
      )
      prepareCompleted = true
      session = provider.createSession({
        pairingCode,
        credentialId: prepared.credentialId,
        handshakeId: BigInt(`0x${prepared.handshakeId}`),
        deviceId: hexToBytes(prepared.deviceId),
        clientNonce,
        deviceNonce: hexToBytes(prepared.deviceNonce),
      })
      const clientRoundOne = session.writeRoundOne()
      const roundOne = binaryResultSchema('deviceRoundOne', 512).parse(
        await this.requestPublic('pairing.begin', {
          handshakeId: prepared.handshakeId,
          clientRoundOne: bytesToHex(clientRoundOne),
        }),
      ) as Record<string, string>
      session.readRoundOne(hexToBytes(roundOne.deviceRoundOne ?? ''))

      const clientRoundTwo = session.writeRoundTwo()
      const roundTwo = binaryResultSchema('deviceRoundTwo', 512).parse(
        await this.requestPublic('pairing.round2', {
          handshakeId: prepared.handshakeId,
          clientRoundTwo: bytesToHex(clientRoundTwo),
        }),
      ) as Record<string, string>
      session.readRoundTwo(hexToBytes(roundTwo.deviceRoundTwo ?? ''))

      const clientFinished = session.createFinished()
      const finished = binaryResultSchema('deviceFinished', 32).parse(
        await this.requestPublic('pairing.finish', {
          handshakeId: prepared.handshakeId,
          clientFinished: bytesToHex(clientFinished),
        }),
      ) as Record<string, string>
      session.verifyFinished(hexToBytes(finished.deviceFinished ?? ''))
      this.cryptoSession?.destroy()
      this.cryptoSession = session
      session = null
    } catch (error) {
      session?.destroy()
      if (
        !prepareCompleted &&
        error instanceof DeviceManagementError &&
        error.code === 'PAIRING_UNAVAILABLE'
      ) {
        throw new Error('请先连接设备并短按 BOOT 三次，再输入屏幕配对码')
      }
      await this.close()
      throw new Error('安全配对失败，请重新执行设备确认')
    } finally {
      clientNonce.fill(0)
    }
  }

  async requestSecure(method: string, params?: unknown): Promise<unknown> {
    return this.serialize(async () => {
      if (this.cryptoSession === null) throw new Error('设备尚未安全配对')
      const request = this.requestPayload(method, params)
      let encrypted: Uint8Array | null = null
      let plaintext: Uint8Array | null = null
      try {
        encrypted = this.cryptoSession.encryptRequest(request.payload)
        await withTimeout(
          this.writer.write(encodeManagementFrame('request', encrypted)),
        )
        const frame = await this.readFrame()
        if (frame.kind !== 'response') throw new Error('设备管理响应无效')
        plaintext = this.cryptoSession.decryptResponse('response', frame.payload)
        return resultFromEnvelope(parseEnvelope(plaintext, request.id))
      } finally {
        request.payload.fill(0)
        encrypted?.fill(0)
        plaintext?.fill(0)
      }
    })
  }

  async close(): Promise<void> {
    if (this.closed) return
    this.closed = true
    this.cryptoSession?.destroy()
    this.cryptoSession = null
    this.frames.reset()
    const bestEffort = async (operation: () => void | Promise<void>) => {
      try {
        await withTimeout(Promise.resolve().then(operation), 1_000)
      } catch {
        // Every cleanup step is independent and bounded.
      }
    }
    await bestEffort(() => this.reader.cancel())
    await bestEffort(() => this.writer.abort())
    await bestEffort(() => this.reader.releaseLock())
    await bestEffort(() => this.writer.releaseLock())
    await bestEffort(() => setSerialPortIdle(this.port))
    await bestEffort(() => this.port.close())
  }
}
