import { describe, expect, it, vi } from 'vitest'

import { ManagementFrameDecoder, encodeManagementFrame } from './serialFrameCodec'
import {
  SerialHelloClient,
  browserSerialApi,
  type SerialApi,
  type SerialPort,
  type SerialReader,
  type SerialWriter,
} from './serialHelloClient'

const encoder = new TextEncoder()

function makePort(responseChunks: readonly Uint8Array[]) {
  const writes: Uint8Array[] = []
  let readIndex = 0
  const reader: SerialReader = {
    read: vi.fn(async () => {
      const value = responseChunks[readIndex]
      readIndex += 1
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
  return { api, port, reader, writer, writes }
}

function helloResponse(): Uint8Array {
  return encodeManagementFrame(
    'response',
    encoder.encode(
      JSON.stringify({
        v: 1,
        id: 1,
        ok: true,
        result: {
          protocol: 'aiqa-management',
          version: 1,
          maxFrameBytes: 4096,
          authentication: 'authentication_required',
        },
      }),
    ),
  )
}

describe('SerialHelloClient', () => {
  it('detects whether the browser exposes Web Serial', () => {
    const serial: SerialApi = { requestPort: vi.fn() }

    expect(browserSerialApi({ serial } as Navigator & { serial: SerialApi })).toBe(
      serial,
    )
    expect(browserSerialApi({} as Navigator)).toBeNull()
  })

  it('rejects an invalid timeout before requesting a device', () => {
    const serial: SerialApi = { requestPort: vi.fn() }

    expect(() => new SerialHelloClient(serial, 0)).toThrow('握手超时配置无效')
    expect(serial.requestPort).not.toHaveBeenCalled()
  })

  it('requests a port and validates a split hello response', async () => {
    const response = helloResponse()
    const fixture = makePort([response.slice(0, 7), response.slice(7)])

    const hello = await new SerialHelloClient(fixture.api).connectAndHello()

    expect(hello.authentication).toBe('authentication_required')
    expect(fixture.port.open).toHaveBeenCalledWith({ baudRate: 115200 })
    expect(fixture.port.setSignals).toHaveBeenCalledTimes(2)
    expect(fixture.port.setSignals).toHaveBeenLastCalledWith({
      dataTerminalReady: false,
      requestToSend: false,
    })
    expect(fixture.port.close).toHaveBeenCalledOnce()
    expect(fixture.reader.releaseLock).toHaveBeenCalledOnce()
    expect(fixture.writer.releaseLock).toHaveBeenCalledOnce()

    const requestFrames = new ManagementFrameDecoder().push(fixture.writes[0]!)
    expect(requestFrames).toHaveLength(1)
    expect(requestFrames[0]?.kind).toBe('request')
    expect(JSON.parse(new TextDecoder().decode(requestFrames[0]?.payload))).toEqual({
      id: 1,
      method: 'system.hello',
    })
  })

  it('does not expose untrusted device error details', async () => {
    const response = encodeManagementFrame(
      'response',
      encoder.encode(
        JSON.stringify({
          v: 1,
          id: 1,
          ok: false,
          error: { code: 'AUTHENTICATION_REQUIRED', message: 'secret leak' },
        }),
      ),
    )
    const fixture = makePort([response])

    await expect(
      new SerialHelloClient(fixture.api).connectAndHello(),
    ).rejects.toThrow('设备握手失败')
  })

  it('rejects a device that closes the stream before replying', async () => {
    const fixture = makePort([])

    await expect(new SerialHelloClient(fixture.api).connectAndHello()).rejects.toThrow(
      '设备握手失败',
    )
  })

  it('ignores an empty read before the hello response', async () => {
    const fixture = makePort([helloResponse()])
    vi.mocked(fixture.reader.read).mockResolvedValueOnce({
      done: false,
      value: undefined,
    })

    await expect(new SerialHelloClient(fixture.api).connectAndHello()).resolves.toMatchObject({
      protocol: 'aiqa-management',
    })
  })

  it('rejects an inbound frame with the wrong direction', async () => {
    const request = encodeManagementFrame(
      'request',
      encoder.encode('{"id":1,"method":"system.hello"}'),
    )
    const fixture = makePort([request])

    await expect(new SerialHelloClient(fixture.api).connectAndHello()).rejects.toThrow(
      '设备握手失败',
    )
  })

  it('closes a port that does not expose readable and writable streams', async () => {
    const port: SerialPort = {
      open: vi.fn(async () => undefined),
      setSignals: vi.fn(async () => undefined),
      close: vi.fn(async () => undefined),
      readable: null,
      writable: null,
    }
    const serial: SerialApi = { requestPort: vi.fn(async () => port) }

    await expect(new SerialHelloClient(serial).connectAndHello()).rejects.toThrow(
      '设备握手失败',
    )
    expect(port.setSignals).toHaveBeenCalledTimes(2)
    expect(port.close).toHaveBeenCalledOnce()
  })

  it('cancels a stalled read and closes the selected port', async () => {
    const fixture = makePort([])
    vi.mocked(fixture.reader.read).mockImplementation(
      () => new Promise(() => undefined),
    )

    await expect(
      new SerialHelloClient(fixture.api, 5).connectAndHello(),
    ).rejects.toThrow('设备握手超时')
    expect(fixture.reader.cancel).toHaveBeenCalledOnce()
    expect(fixture.port.close).toHaveBeenCalledOnce()
  })

  it('applies the handshake deadline to opening the port', async () => {
    const fixture = makePort([])
    vi.mocked(fixture.port.open).mockImplementation(
      () => new Promise(() => undefined),
    )

    await expect(
      new SerialHelloClient(fixture.api, 5).connectAndHello(),
    ).rejects.toThrow('设备握手超时')
    expect(fixture.port.close).toHaveBeenCalledOnce()
  })

  it('applies the handshake deadline to writing the request', async () => {
    const fixture = makePort([])
    vi.mocked(fixture.writer.write).mockImplementation(
      () => new Promise(() => undefined),
    )

    await expect(
      new SerialHelloClient(fixture.api, 5).connectAndHello(),
    ).rejects.toThrow('设备握手超时')
    expect(fixture.reader.cancel).toHaveBeenCalledOnce()
    expect(fixture.writer.releaseLock).toHaveBeenCalledOnce()
    expect(fixture.port.close).toHaveBeenCalledOnce()
  })

  it('preserves the timeout when every cleanup step fails', async () => {
    const fixture = makePort([])
    vi.mocked(fixture.reader.read).mockImplementation(
      () => new Promise(() => undefined),
    )
    vi.mocked(fixture.reader.cancel).mockRejectedValue(new Error('cancel leak'))
    vi.mocked(fixture.writer.abort).mockRejectedValue(new Error('abort leak'))
    vi.mocked(fixture.reader.releaseLock).mockImplementation(() => {
      throw new Error('reader leak')
    })
    vi.mocked(fixture.writer.releaseLock).mockImplementation(() => {
      throw new Error('writer leak')
    })
    vi.mocked(fixture.port.close).mockRejectedValue(new Error('close leak'))

    await expect(
      new SerialHelloClient(fixture.api, 5).connectAndHello(),
    ).rejects.toThrow('设备握手超时')
    expect(fixture.writer.releaseLock).toHaveBeenCalledOnce()
    expect(fixture.port.close).toHaveBeenCalledOnce()
  })

  it('sanitizes device picker failures', async () => {
    const serial: SerialApi = {
      requestPort: vi.fn(async () => {
        throw new Error('private browser detail')
      }),
    }

    await expect(new SerialHelloClient(serial).connectAndHello()).rejects.toThrow(
      '设备握手失败',
    )
  })
})
