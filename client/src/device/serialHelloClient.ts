import { z } from 'zod'

import { ManagementFrameDecoder, encodeManagementFrame } from './serialFrameCodec'

export interface SerialReader {
  read(): Promise<ReadableStreamReadResult<Uint8Array>>
  cancel(): Promise<void>
  releaseLock(): void
}

export interface SerialWriter {
  write(value: Uint8Array): Promise<void>
  abort(reason?: unknown): Promise<void>
  releaseLock(): void
}

export interface SerialPort {
  readonly readable: { getReader(): SerialReader } | null
  readonly writable: { getWriter(): SerialWriter } | null
  open(options: { readonly baudRate: number }): Promise<void>
  close(): Promise<void>
}

export interface SerialApi {
  requestPort(): Promise<SerialPort>
}

export interface SerialHello {
  readonly protocol: 'aiqa-management'
  readonly version: 1
  readonly maxFrameBytes: 4096
  readonly authentication: 'authentication_required'
}

const helloEnvelopeSchema = z
  .object({
    v: z.literal(1),
    id: z.literal(1),
    ok: z.literal(true),
    result: z
      .object({
        protocol: z.literal('aiqa-management'),
        version: z.literal(1),
        maxFrameBytes: z.literal(4096),
        authentication: z.literal('authentication_required'),
      })
      .strict(),
  })
  .strict()

const encoder = new TextEncoder()
const decoder = new TextDecoder('utf-8', { fatal: true })
const CLEANUP_TIMEOUT_MS = 250

class HelloTimeoutError extends Error {}

function parseHello(payload: Uint8Array): SerialHello {
  try {
    const parsed: unknown = JSON.parse(decoder.decode(payload))
    return helloEnvelopeSchema.parse(parsed).result
  } catch {
    throw new Error('设备握手失败')
  }
}

async function readHello(reader: SerialReader): Promise<SerialHello> {
  const frames = new ManagementFrameDecoder()
  for (;;) {
    const result = await reader.read()
    if (result.done) {
      throw new Error('设备握手失败')
    }
    if (result.value === undefined) {
      continue
    }
    for (const frame of frames.push(result.value)) {
      if (frame.kind !== 'response') {
        throw new Error('设备握手失败')
      }
      return parseHello(frame.payload)
    }
  }
}

function withTimeout<T>(operation: Promise<T>, timeoutMs: number): {
  readonly result: Promise<T>
  readonly cancelTimer: () => void
} {
  let timer: ReturnType<typeof setTimeout> | undefined
  const timeout = new Promise<never>((_resolve, reject) => {
    timer = setTimeout(() => reject(new HelloTimeoutError()), timeoutMs)
  })
  return {
    result: Promise.race([operation, timeout]),
    cancelTimer: () => {
      if (timer !== undefined) clearTimeout(timer)
    },
  }
}

async function boundedBestEffort(operation: () => void | Promise<void>): Promise<void> {
  let timer: ReturnType<typeof setTimeout> | undefined
  try {
    const timeout = new Promise<void>((resolve) => {
      timer = setTimeout(resolve, CLEANUP_TIMEOUT_MS)
    })
    await Promise.race([Promise.resolve(operation()), timeout])
  } catch {
    // Cleanup failures must never mask the sanitized handshake result.
  } finally {
    if (timer !== undefined) clearTimeout(timer)
  }
}

export function browserSerialApi(
  navigatorValue: Navigator = navigator,
): SerialApi | null {
  const candidate = navigatorValue as Navigator & { readonly serial?: SerialApi }
  return candidate.serial ?? null
}

export class SerialHelloClient {
  constructor(
    private readonly serial: SerialApi,
    private readonly timeoutMs = 3000,
  ) {
    if (!Number.isSafeInteger(timeoutMs) || timeoutMs <= 0) {
      throw new Error('握手超时配置无效')
    }
  }

  async connectAndHello(): Promise<SerialHello> {
    let port: SerialPort
    try {
      port = await this.serial.requestPort()
    } catch {
      throw new Error('设备握手失败')
    }
    let reader: SerialReader | undefined
    let writer: SerialWriter | undefined
    let abandoned = false
    let pending: ReturnType<typeof withTimeout<SerialHello>> | undefined

    const cleanup = async (abortPending: boolean): Promise<void> => {
      if (abortPending) {
        await boundedBestEffort(() => reader?.cancel())
        await boundedBestEffort(() => writer?.abort('handshake ended'))
      }
      await boundedBestEffort(() => reader?.releaseLock())
      await boundedBestEffort(() => writer?.releaseLock())
      await boundedBestEffort(() => port.close())
    }

    try {
      const handshake = (async () => {
        await port.open({ baudRate: 115200 })
        if (abandoned) throw new HelloTimeoutError()
        if (port.readable === null || port.writable === null) {
          throw new Error('设备 handshake streams unavailable')
        }
        reader = port.readable.getReader()
        writer = port.writable.getWriter()

        const request = encodeManagementFrame(
          'request',
          encoder.encode(JSON.stringify({ id: 1, method: 'system.hello' })),
        )
        await writer.write(request)
        if (abandoned) throw new HelloTimeoutError()
        return readHello(reader)
      })()
      void handshake.then(
        () => (abandoned ? cleanup(true) : undefined),
        () => (abandoned ? cleanup(true) : undefined),
      )
      pending = withTimeout(handshake, this.timeoutMs)
      try {
        return await pending.result
      } catch (error) {
        if (error instanceof HelloTimeoutError) {
          abandoned = true
          throw new Error('设备握手超时')
        }
        throw error
      }
    } catch (error) {
      if (error instanceof Error && error.message === '设备握手超时') {
        throw error
      }
      throw new Error('设备握手失败')
    } finally {
      pending?.cancelTimer()
      await cleanup(abandoned)
    }
  }
}
