import { describe, expect, it, vi } from 'vitest'

import { PhysicalDeviceTransport } from './physicalDeviceTransport'
import type { SerialManagementConnection } from './serialManagementConnection'

const publicConfig = {
  revision: 4,
  wifi: { ssid: 'Office', hasPassword: true },
  chatProvider: 'dashscope_openai_chat',
  chatModel: 'qwen3.7-max',
  asrProvider: 'qwen_asr',
  asrModel: 'qwen3-asr-flash',
  ttsProvider: 'qwen_tts',
  ttsModel: 'qwen3-tts-flash',
  ttsVoice: 'Cherry',
  stream: true,
  hideReasoning: true,
  maxCompletionTokens: 256,
  hasChatApiKey: true,
  hasAsrApiKey: true,
  userPrefs: {
    volumePercent: 50,
    assistantProfile: { name: 'AIQA', gender: 'neutral' },
  },
}

const status = {
  sequence: 1,
  state: 'IDLE',
  error: 'NONE',
  uptimeMs: 1000,
  freeHeapBytes: 100_000,
  wifi: { connected: true, rssiDbm: -45 },
  power: { batteryPresent: true, percent: 80, chargingState: 'DISCHARGING' },
  ui: { status: 'READY', detail: null, hint: 'HOLD BOOT', expression: 'IDLE' },
  config: {
    available: true,
    revision: 4,
    chatProvider: 'dashscope_openai_chat',
    chatModel: 'qwen3.7-max',
    hasChatApiKey: true,
    hasAsrApiKey: true,
  },
  latestOperation: { id: 8, state: 'SUCCEEDED', result: 'OK' },
}

describe('PhysicalDeviceTransport', () => {
  it('projects encrypted public configuration into the Wi-Fi view', async () => {
    const requestSecure = vi.fn().mockResolvedValue(publicConfig)
    const transport = new PhysicalDeviceTransport({ requestSecure } as unknown as SerialManagementConnection)

    await expect(transport.getWifiConfig()).resolves.toEqual({
      revision: 4,
      ssid: 'Office',
      hasPassword: true,
    })
    expect(requestSecure).toHaveBeenCalledWith('config.public.get')
  })

  it('submits Wi-Fi and waits for the matching asynchronous terminal state', async () => {
    const requestSecure = vi
      .fn()
      .mockResolvedValueOnce({ operationId: 8, state: 'PENDING' })
      .mockResolvedValueOnce(status)
      .mockResolvedValueOnce(publicConfig)
    const transport = new PhysicalDeviceTransport({ requestSecure } as unknown as SerialManagementConnection)

    await expect(
      transport.updateWifi({
        baseRevision: 3,
        ssid: 'Office',
        passwordAction: 'replace',
        password: 'secret123',
      }),
    ).resolves.toMatchObject({ revision: 4, ssid: 'Office' })
    expect(requestSecure.mock.calls[0]?.[0]).toBe('wifi.update')
    expect(requestSecure.mock.calls[1]?.[0]).toBe('device.status.get')
    expect(requestSecure.mock.calls[2]?.[0]).toBe('config.public.get')
  })
})
