import { useCallback, useEffect, useRef, useState } from 'react'

import type { DeviceTransport } from '../device/deviceTransport'
import type { DeviceStatus } from '../device/deviceStatus'

interface DeviceOverviewProps {
  readonly transport: DeviceTransport
  readonly onStatus?: (status: DeviceStatus) => void
}

function formatHeap(bytes: number): string {
  return `${Math.round(bytes / 1024)} KB`
}

function formatUptime(milliseconds: number): string {
  const totalMinutes = Math.floor(milliseconds / 60_000)
  const hours = Math.floor(totalMinutes / 60)
  const minutes = totalMinutes % 60
  return hours > 0 ? `${hours} 小时 ${minutes} 分` : `${minutes} 分钟`
}

export function DeviceOverview({ transport, onStatus }: DeviceOverviewProps) {
  const [status, setStatus] = useState<DeviceStatus | null>(null)
  const [error, setError] = useState('')
  const [loading, setLoading] = useState(false)
  const requestSequence = useRef(0)

  const loadStatus = useCallback(async () => {
    const requestId = requestSequence.current + 1
    requestSequence.current = requestId
    setError('')
    setLoading(true)
    try {
      const nextStatus = await transport.getDeviceStatus()
      if (requestSequence.current === requestId) {
        setStatus(nextStatus)
        onStatus?.(nextStatus)
      }
    } catch {
      if (requestSequence.current === requestId) {
        setError('无法读取设备状态')
      }
    } finally {
      if (requestSequence.current === requestId) {
        setLoading(false)
      }
    }
  }, [onStatus, transport])

  useEffect(() => {
    void loadStatus()
  }, [loadStatus])

  if (status === null) {
    if (error) {
      return (
        <div className="load-error" role="alert">
          <p>{error}</p>
          <button onClick={() => void loadStatus()} type="button">重新读取</button>
        </div>
      )
    }
    return <div className="loading-line">正在读取设备状态…</div>
  }

  const rssi = status.wifi.rssiDbm === null ? '不可用' : `${String(status.wifi.rssiDbm).replace('-', '–')} dBm`
  const battery = status.power.percent === null ? '不可用' : `${status.power.percent}%`

  return (
    <section className="device-overview">
      <div className="section-heading">
        <div>
          <p className="eyebrow">LIVE SNAPSHOT / {status.sequence}</p>
          <h2>设备概览</h2>
        </div>
        <button
          className="secondary-action"
          disabled={loading}
          onClick={() => void loadStatus()}
          type="button"
        >
          刷新状态
        </button>
      </div>

      {error ? <p className="inline-error" role="alert">{error}</p> : null}

      <div className="overview-grid">
        <article><span>运行状态</span><strong>{status.state}</strong><small>{status.error}</small></article>
        <article><span>屏幕界面</span><strong>{status.ui.status}</strong><small>{status.ui.hint ?? '—'}</small></article>
        <article><span>Wi-Fi 信号</span><strong>{rssi}</strong><small>{status.wifi.connected ? '已连接' : '未连接'}</small></article>
        <article><span>电池</span><strong>{battery}</strong><small>{status.power.chargingState}</small></article>
        <article><span>可用内存</span><strong>{formatHeap(status.freeHeapBytes)}</strong><small>运行 {formatUptime(status.uptimeMs)}</small></article>
        <article><span>配置版本</span><strong>REV {status.config.revision}</strong><small>事务快照</small></article>
      </div>

      <div className="model-summary">
        <div><span>模型服务商</span><strong>{status.config.chatProvider ?? '未配置'}</strong></div>
        <div><span>聊天模型</span><strong>{status.config.chatModel ?? '未配置'}</strong></div>
        <div><span>Chat 密钥</span><strong>{status.config.hasChatApiKey ? '已配置' : '未配置'}</strong></div>
        <div><span>ASR 密钥</span><strong>{status.config.hasAsrApiKey ? '已配置' : '未配置'}</strong></div>
        <div>
          <span>最近配置任务</span>
          <strong>
            {status.latestOperation.id === 0
              ? '无'
              : `#${status.latestOperation.id} · ${status.latestOperation.state}`}
          </strong>
        </div>
      </div>
    </section>
  )
}
