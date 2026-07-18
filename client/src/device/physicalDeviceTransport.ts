import { parseDeviceStatus, type DeviceStatus } from './deviceStatus'
import {
  RevisionConflictError,
  RevisionExhaustedError,
  type DeviceTransport,
} from './deviceTransport'
import { parsePublicConfig } from './publicConfig'
import {
  DeviceManagementError,
  SerialManagementConnection,
} from './serialManagementConnection'
import {
  parseWifiPublicConfig,
  parseWifiUpdate,
  type WifiPublicConfig,
  type WifiUpdate,
} from './wifiConfig'

const POLL_INTERVAL_MS = 500
const OPERATION_TIMEOUT_MS = 60_000

function delay(milliseconds: number): Promise<void> {
  return new Promise((resolve) => setTimeout(resolve, milliseconds))
}

export class PhysicalDeviceTransport implements DeviceTransport {
  readonly mode = 'physical'

  constructor(private readonly connection: SerialManagementConnection) {}

  async getDeviceStatus(): Promise<DeviceStatus> {
    return parseDeviceStatus(
      await this.connection.requestSecure('device.status.get'),
    )
  }

  async getWifiConfig(): Promise<WifiPublicConfig> {
    const config = parsePublicConfig(
      await this.connection.requestSecure('config.public.get'),
    )
    return parseWifiPublicConfig({
      revision: config.revision,
      ssid: config.wifi.ssid,
      hasPassword: config.wifi.hasPassword,
    })
  }

  async updateWifi(input: WifiUpdate): Promise<WifiPublicConfig> {
    const update = parseWifiUpdate(input)
    let accepted: unknown
    try {
      accepted = await this.connection.requestSecure('wifi.update', update)
    } catch (error) {
      if (error instanceof DeviceManagementError) {
        if (error.code === 'REVISION_CONFLICT') {
          const current = await this.getWifiConfig()
          throw new RevisionConflictError(current.revision)
        }
        if (error.code === 'REVISION_EXHAUSTED') throw new RevisionExhaustedError()
      }
      throw error
    }
    if (
      typeof accepted !== 'object' ||
      accepted === null ||
      !('operationId' in accepted) ||
      typeof accepted.operationId !== 'number' ||
      !Number.isInteger(accepted.operationId) ||
      accepted.operationId <= 0
    ) {
      throw new Error('设备配置任务响应无效')
    }
    const operationId = accepted.operationId
    const deadline = Date.now() + OPERATION_TIMEOUT_MS
    while (Date.now() < deadline) {
      const status = await this.getDeviceStatus()
      if (status.latestOperation.id === operationId) {
        if (status.latestOperation.state === 'SUCCEEDED') return this.getWifiConfig()
        if (status.latestOperation.state === 'FAILED') {
          throw new DeviceManagementError(status.latestOperation.result)
        }
      } else if (status.latestOperation.id > operationId) {
        throw new Error('设备配置任务状态已丢失')
      }
      await delay(POLL_INTERVAL_MS)
    }
    throw new Error('设备网络验证超时')
  }

  async close(): Promise<void> {
    await this.connection.close()
  }
}
