import {
  parseWifiPublicConfig,
  parseWifiUpdate,
  type WifiPublicConfig,
  type WifiUpdate,
} from './wifiConfig'

export interface DeviceTransport {
  readonly mode: 'simulated' | 'physical'
  getWifiConfig(): Promise<WifiPublicConfig>
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

  constructor(initialState: WifiPublicConfig = INITIAL_STATE) {
    this.state = parseWifiPublicConfig(initialState)
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
    return parseWifiPublicConfig(toPublicConfig(nextState))
  }
}
