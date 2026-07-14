import { describe, expect, it } from 'vitest'

import { parseDeviceStatus } from './deviceStatus'

const VALID_STATUS = {
  sequence: 7,
  state: 'IDLE',
  error: 'NONE',
  uptimeMs: 65_432,
  freeHeapBytes: 188_416,
  wifi: { connected: true, rssiDbm: -48 },
  power: {
    batteryPresent: true,
    percent: 82,
    chargingState: 'DISCHARGING',
  },
  ui: {
    status: 'READY',
    detail: null,
    hint: 'HOLD BOOT',
    expression: 'IDLE',
  },
  config: {
    available: true,
    revision: 3,
    chatProvider: 'dashscope_openai_chat',
    chatModel: 'qwen3.7-max',
    hasChatApiKey: true,
    hasAsrApiKey: true,
  },
  latestOperation: { id: 2, state: 'SUCCEEDED', result: 'OK' },
}

describe('parseDeviceStatus', () => {
  it('accepts a complete redacted device status snapshot', () => {
    expect(parseDeviceStatus(VALID_STATUS)).toEqual(VALID_STATUS)
  })

  it('rejects unknown state and out-of-range telemetry', () => {
    expect(() => parseDeviceStatus({ ...VALID_STATUS, state: 'ROOT_SHELL' })).toThrow(
      '设备状态响应无效',
    )
    expect(() =>
      parseDeviceStatus({
        ...VALID_STATUS,
        power: { ...VALID_STATUS.power, percent: 101 },
      }),
    ).toThrow('设备状态响应无效')
  })

  it('rejects secret-bearing response fields', () => {
    expect(() =>
      parseDeviceStatus({
        ...VALID_STATUS,
        config: { ...VALID_STATUS.config, chatApiKey: 'must-not-leak' },
      }),
    ).toThrow('设备状态响应无效')
  })

  it('accepts a status snapshot before provisioning is available', () => {
    expect(
      parseDeviceStatus({
        ...VALID_STATUS,
        config: {
          available: false,
          revision: 0,
          chatProvider: null,
          chatModel: null,
          hasChatApiKey: false,
          hasAsrApiKey: false,
        },
      }).config,
    ).toMatchObject({ available: false, revision: 0 })
  })
})
