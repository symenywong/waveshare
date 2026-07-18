import { describe, expect, it, vi } from 'vitest'

import type {
  PairingCryptoProvider,
  PairingCryptoSession,
} from './pairingCryptoProvider'
import { encodeManagementFrame } from './serialFrameCodec'
import { SerialManagementConnection } from './serialManagementConnection'
import type {
  SerialApi,
  SerialPort,
  SerialReader,
  SerialWriter,
} from './serialHelloClient'

const encoder = new TextEncoder()

function response(id: number, result: unknown): Uint8Array {
  return encodeManagementFrame(
    'response',
    encoder.encode(JSON.stringify({ v: 1, id, ok: true, result })),
  )
}

function errorResponse(id: number, code: string): Uint8Array {
  return encodeManagementFrame(
    'response',
    encoder.encode(JSON.stringify({ v: 1, id, ok: false, error: { code } })),
  )
}

function helloResponse(): Uint8Array {
  return response(1, {
    protocol: 'aiqa-management',
    version: 1,
    maxFrameBytes: 4096,
    authentication: 'authentication_required',
  })
}

function makePort(initial: readonly Uint8Array[] = [helloResponse()]) {
  const reads = [...initial]
  const writes: Uint8Array[] = []
  const reader: SerialReader = {
    read: vi.fn(async () => {
      const value = reads.shift()
      return value === undefined
        ? ({ done: true, value: undefined } as const)
        : ({ done: false, value } as const)
    }),
    cancel: vi.fn(async () => undefined),
    releaseLock: vi.fn(),
  }
  const writer: SerialWriter = {
    write: vi.fn(async (value) => {
      writes.push(value.slice())
    }),
    abort: vi.fn(async () => undefined),
    releaseLock: vi.fn(),
  }
  const port: SerialPort = {
    open: vi.fn(async () => undefined),
    setSignals: vi.fn(async () => undefined),
    close: vi.fn(async () => undefined),
    readable: { getReader: () => reader },
    writable: { getWriter: () => writer },
  }
  const api: SerialApi = { requestPort: vi.fn(async () => port) }
  return { api, port, reader, writer, reads, writes }
}

function fakePairing() {
  const decrypted = { value: new Uint8Array() }
  const session: PairingCryptoSession = {
    writeRoundOne: vi.fn(() => Uint8Array.from([0x11, 0x12])),
    readRoundOne: vi.fn(),
    writeRoundTwo: vi.fn(() => Uint8Array.from([0x21, 0x22])),
    readRoundTwo: vi.fn(),
    createFinished: vi.fn(() => new Uint8Array(32).fill(0x31)),
    verifyFinished: vi.fn(),
    encryptRequest: vi.fn((value) => value.slice()),
    decryptResponse: vi.fn(() => decrypted.value.slice()),
    destroy: vi.fn(),
  }
  const provider: PairingCryptoProvider = {
    createSession: vi.fn(() => session),
  }
  return { provider, session, decrypted }
}

describe('SerialManagementConnection', () => {
  it('completes pairing and keeps the serial port for authenticated requests', async () => {
    const fixture = makePort()
    const connection = await SerialManagementConnection.connect(fixture.api)
    expect(fixture.port.setSignals).toHaveBeenCalledWith({
      dataTerminalReady: false,
      requestToSend: false,
    })
    const pairing = fakePairing()
    fixture.reads.push(
      response(2, {
        credentialId: 1,
        handshakeId: '0102030405060708',
        deviceId: '0102',
        deviceNonce: '03'.repeat(32),
      }),
      response(3, { deviceRoundOne: 'a1a2' }),
      response(4, { deviceRoundTwo: 'b1b2' }),
      response(5, { deviceFinished: 'c1'.repeat(32) }),
    )

    await connection.pair('12345678', pairing.provider)

    expect(pairing.provider.createSession).toHaveBeenCalledWith(
      expect.objectContaining({
        pairingCode: '12345678',
        credentialId: 1,
        handshakeId: 0x0102030405060708n,
        deviceId: Uint8Array.from([1, 2]),
      }),
    )
    expect(pairing.session.readRoundOne).toHaveBeenCalledWith(
      Uint8Array.from([0xa1, 0xa2]),
    )
    expect(pairing.session.readRoundTwo).toHaveBeenCalledWith(
      Uint8Array.from([0xb1, 0xb2]),
    )
    expect(pairing.session.verifyFinished).toHaveBeenCalledWith(
      new Uint8Array(32).fill(0xc1),
    )

    pairing.decrypted.value = encoder.encode(
      JSON.stringify({ v: 1, id: 6, ok: true, result: { state: 'READY' } }),
    )
    fixture.reads.push(
      encodeManagementFrame('response', new Uint8Array(44).fill(0x55)),
    )
    await expect(connection.requestSecure('device.status.get')).resolves.toEqual({
      state: 'READY',
    })
    expect(pairing.session.encryptRequest).toHaveBeenCalled()
    expect(pairing.session.decryptResponse).toHaveBeenCalledWith(
      'response',
      new Uint8Array(44).fill(0x55),
    )
    expect(fixture.port.close).not.toHaveBeenCalled()

    await connection.close()
    await connection.close()
    expect(pairing.session.destroy).toHaveBeenCalledOnce()
    expect(fixture.reader.cancel).toHaveBeenCalledOnce()
    expect(fixture.port.setSignals).toHaveBeenLastCalledWith({
      dataTerminalReady: false,
      requestToSend: false,
    })
    expect(fixture.port.setSignals).toHaveBeenCalledTimes(2)
    expect(fixture.port.close).toHaveBeenCalledOnce()
    await expect(connection.requestPublic('pairing.status')).rejects.toThrow(
      '设备连接已关闭',
    )
  })

  it('keeps a sanitized device error recoverable and maps pairing availability', async () => {
    const fixture = makePort()
    const connection = await SerialManagementConnection.connect(fixture.api)
    fixture.reads.push(errorResponse(2, 'PAIRING_UNAVAILABLE'))

    await expect(connection.pair('12345678', fakePairing().provider)).rejects.toThrow(
      '请先连接设备并短按 BOOT 三次',
    )
    expect(fixture.port.close).not.toHaveBeenCalled()

    fixture.reads.push(errorResponse(3, 'REVISION_CONFLICT'))
    await expect(connection.requestPublic('test')).rejects.toMatchObject({
      code: 'REVISION_CONFLICT',
    })
    expect(fixture.port.close).not.toHaveBeenCalled()
    await connection.close()
  })

  it('rejects invalid codes and closes after unauthenticated secure access', async () => {
    const fixture = makePort()
    const connection = await SerialManagementConnection.connect(fixture.api)

    await expect(connection.pair('12ab', fakePairing().provider)).rejects.toThrow(
      '请输入设备屏幕上的 8 位配对码',
    )
    await expect(connection.requestSecure('device.status.get')).rejects.toThrow(
      '设备尚未安全配对',
    )
    expect(fixture.port.close).toHaveBeenCalledOnce()
  })

  it('sanitizes connection failures and closes ports without streams', async () => {
    const port: SerialPort = {
      open: vi.fn(async () => undefined),
      setSignals: vi.fn(async () => undefined),
      close: vi.fn(async () => undefined),
      readable: null,
      writable: null,
    }
    const api: SerialApi = { requestPort: vi.fn(async () => port) }

    await expect(SerialManagementConnection.connect(api)).rejects.toThrow(
      '设备连接失败',
    )
    expect(port.setSignals).toHaveBeenCalledTimes(2)
    expect(port.setSignals).toHaveBeenLastCalledWith({
      dataTerminalReady: false,
      requestToSend: false,
    })
    expect(port.close).toHaveBeenCalledOnce()
  })

  it('releases both stream locks when hello validation fails', async () => {
    const fixture = makePort([response(1, { protocol: 'wrong-device' })])

    await expect(SerialManagementConnection.connect(fixture.api)).rejects.toThrow(
      '设备连接失败',
    )
    expect(fixture.reader.cancel).toHaveBeenCalledOnce()
    expect(fixture.writer.abort).toHaveBeenCalledOnce()
    expect(fixture.reader.releaseLock).toHaveBeenCalledOnce()
    expect(fixture.writer.releaseLock).toHaveBeenCalledOnce()
    expect(fixture.port.close).toHaveBeenCalledOnce()
  })

  it('terminates the transport after a post-prepare pairing failure', async () => {
    const fixture = makePort()
    const connection = await SerialManagementConnection.connect(fixture.api)
    const pairing = fakePairing()
    fixture.reads.push(
      response(2, {
        credentialId: 1,
        handshakeId: '0102030405060708',
        deviceId: '01',
        deviceNonce: '03'.repeat(32),
      }),
      response(3, { deviceRoundOne: 'not-hex' }),
    )

    await expect(connection.pair('12345678', pairing.provider)).rejects.toThrow(
      '安全配对失败',
    )
    expect(connection.isClosed).toBe(true)
    expect(pairing.session.destroy).toHaveBeenCalledOnce()
    expect(fixture.port.close).toHaveBeenCalledOnce()
  })

  it('does not trust a recoverable error code after prepare completes', async () => {
    const fixture = makePort()
    const connection = await SerialManagementConnection.connect(fixture.api)
    const pairing = fakePairing()
    fixture.reads.push(
      response(2, {
        credentialId: 1,
        handshakeId: '0102030405060708',
        deviceId: '01',
        deviceNonce: '03'.repeat(32),
      }),
      errorResponse(3, 'PAIRING_UNAVAILABLE'),
    )

    await expect(connection.pair('12345678', pairing.provider)).rejects.toThrow(
      '安全配对失败',
    )
    expect(pairing.session.destroy).toHaveBeenCalledOnce()
    expect(connection.isClosed).toBe(true)
    expect(fixture.writer.abort).toHaveBeenCalledOnce()
    expect(fixture.port.close).toHaveBeenCalledOnce()
  })

  it('uses queued frames and rejects a response with the wrong direction', async () => {
    const combined = new Uint8Array([
      ...helloResponse(),
      ...encodeManagementFrame('event', encoder.encode('{}')),
    ])
    const fixture = makePort([combined])
    const connection = await SerialManagementConnection.connect(fixture.api)

    await expect(connection.requestPublic('pairing.status')).rejects.toThrow(
      '设备管理响应无效',
    )
    expect(fixture.port.close).toHaveBeenCalledOnce()
  })

  it('continues every close step when earlier cleanup operations fail', async () => {
    const fixture = makePort()
    const connection = await SerialManagementConnection.connect(fixture.api)
    vi.mocked(fixture.reader.cancel).mockRejectedValue(new Error('cancel failed'))
    vi.mocked(fixture.writer.abort).mockRejectedValue(new Error('abort failed'))
    vi.mocked(fixture.reader.releaseLock).mockImplementation(() => {
      throw new Error('reader unlock failed')
    })

    await expect(connection.close()).resolves.toBeUndefined()
    expect(fixture.writer.abort).toHaveBeenCalledOnce()
    expect(fixture.reader.releaseLock).toHaveBeenCalledOnce()
    expect(fixture.writer.releaseLock).toHaveBeenCalledOnce()
    expect(fixture.port.close).toHaveBeenCalledOnce()
  })
})
