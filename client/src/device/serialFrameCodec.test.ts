import { describe, expect, it } from 'vitest'

import {
  MANAGEMENT_FRAME_MAX_PAYLOAD,
  ManagementFrameDecoder,
  encodeManagementFrame,
} from './serialFrameCodec'

const encoder = new TextEncoder()
const decoder = new TextDecoder()

describe('management serial frame codec', () => {
  it('round-trips a request split at every byte boundary', () => {
    const payload = encoder.encode('{"id":1,"method":"system.hello"}')
    const frame = encodeManagementFrame('request', payload)
    const frames = new ManagementFrameDecoder()

    const decoded = [...frame].flatMap((byte) => frames.push(Uint8Array.of(byte)))

    expect(decoded).toHaveLength(1)
    expect(decoded[0]?.kind).toBe('request')
    expect(decoder.decode(decoded[0]?.payload)).toBe(decoder.decode(payload))
  })

  it('decodes coalesced frames and resynchronizes after noise', () => {
    const first = encodeManagementFrame('request', encoder.encode('{}'))
    const second = encodeManagementFrame('response', encoder.encode('{"ok":true}'))
    const chunk = new Uint8Array(3 + first.length + second.length)
    chunk.set([1, 2, 3])
    chunk.set(first, 3)
    chunk.set(second, 3 + first.length)

    const frames = new ManagementFrameDecoder()
    const decoded = frames.push(chunk)

    expect(decoded.map((frame) => frame.kind)).toEqual(['request', 'response'])
    expect(frames.droppedBytes).toBe(3)
  })

  it('rejects payloads over the fixed device limit', () => {
    expect(() =>
      encodeManagementFrame('request', new Uint8Array(MANAGEMENT_FRAME_MAX_PAYLOAD + 1)),
    ).toThrow('管理帧过大')
  })

  it('rejects unsupported versions and reserved header flags', () => {
    const valid = encodeManagementFrame('request', encoder.encode('{}'))
    const unsupportedVersion = valid.slice()
    unsupportedVersion[4] = 2
    const reservedFlags = valid.slice()
    reservedFlags[6] = 1

    expect(() => new ManagementFrameDecoder().push(unsupportedVersion)).toThrow(
      '设备管理帧头无效',
    )
    expect(() => new ManagementFrameDecoder().push(reservedFlags)).toThrow(
      '设备管理帧头无效',
    )
  })
})
