import {
  parseWifiPublicConfig,
  parseWifiUpdate,
  type WifiPublicConfig,
  type WifiUpdate,
} from './wifiConfig'
import { parseDeviceStatus, type DeviceStatus } from './deviceStatus'

export interface DeviceTransport {
  readonly mode: 'simulated' | 'physical'
  getDeviceStatus(): Promise<DeviceStatus>
  getWifiConfig(): Promise<WifiPublicConfig>
  /* Physical adapters submit, poll latestOperation, then return the refreshed public view. */
  updateWifi(update: WifiUpdate): Promise<WifiPublicConfig>
}

export class RevisionConflictError extends Error {
  constructor(readonly currentRevision: number) {
    super('设备配置已被其他客户端更新，请重新加载')
    this.name = 'RevisionConflictError'
  }
}

export class RevisionExhaustedError extends Error {
  constructor() {
    super('设备配置版本已耗尽，需要维护后才能继续修改')
    this.name = 'RevisionExhaustedError'
  }
}

const INITIAL_STATE: WifiPublicConfig = {
  revision: 1,
  ssid: 'AIQA-Lab',
  hasPassword: true,
}

const INITIAL_DEVICE_STATUS: DeviceStatus = {
  sequence: 1,
  state: 'IDLE',
  error: 'NONE',
  uptimeMs: 65_432,
  freeHeapBytes: 188_416,
  wifi: { connected: true, rssiDbm: -48 },
  power: { batteryPresent: true, percent: 82, chargingState: 'DISCHARGING' },
  ui: { status: 'READY', detail: null, hint: 'HOLD BOOT', expression: 'IDLE' },
  config: {
    available: true,
    revision: 1,
    chatProvider: 'dashscope_openai_chat',
    chatModel: 'qwen3.7-max',
    hasChatApiKey: true,
    hasAsrApiKey: true,
  },
  latestOperation: { id: 0, state: 'NONE', result: 'OK' },
}

function toPublicConfig(state: WifiPublicConfig): WifiPublicConfig {
  return {
    revision: state.revision,
    ssid: state.ssid,
    hasPassword: state.hasPassword,
  }
}

export class SimulatedDeviceTransport implements DeviceTransport {
  readonly mode = 'simulated'
  private state: WifiPublicConfig
  private status: DeviceStatus

  constructor(
    initialState: WifiPublicConfig = INITIAL_STATE,
    initialStatus: DeviceStatus = INITIAL_DEVICE_STATUS,
  ) {
    this.state = parseWifiPublicConfig(initialState)
    this.status = parseDeviceStatus({
      ...initialStatus,
      config: { ...initialStatus.config, revision: initialState.revision },
    })
  }

  async getDeviceStatus(): Promise<DeviceStatus> {
    return parseDeviceStatus(structuredClone(this.status))
  }

  async getWifiConfig(): Promise<WifiPublicConfig> {
    return parseWifiPublicConfig(toPublicConfig(this.state))
  }

  async updateWifi(input: WifiUpdate): Promise<WifiPublicConfig> {
    const update = parseWifiUpdate(input)
    if (update.baseRevision !== this.state.revision) {
      throw new RevisionConflictError(this.state.revision)
    }
    if (this.state.revision === 4_294_967_295) {
      throw new RevisionExhaustedError()
    }

    const hasPassword =
      update.passwordAction === 'keep'
        ? this.state.hasPassword
        : update.passwordAction === 'replace'
    const nextState: WifiPublicConfig = {
      revision: this.state.revision + 1,
      ssid: update.ssid,
      hasPassword,
    }
    this.state = nextState
    this.status = parseDeviceStatus({
      ...this.status,
      sequence: this.status.sequence + 1,
      config: { ...this.status.config, revision: nextState.revision },
      wifi: { ...this.status.wifi, connected: true },
      latestOperation: {
        id: this.status.latestOperation.id + 1,
        state: 'SUCCEEDED',
        result: 'OK',
      },
    })
    return parseWifiPublicConfig(toPublicConfig(nextState))
  }
}
