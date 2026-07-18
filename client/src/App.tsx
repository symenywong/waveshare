import { useEffect, useRef, useState, type FormEvent } from 'react'

import { WifiSettings } from './components/WifiSettings'
import { DeviceOverview } from './components/DeviceOverview'
import { SimulatedDeviceTransport, type DeviceTransport } from './device/deviceTransport'
import type { DeviceStatus } from './device/deviceStatus'
import { browserSerialApi } from './device/serialHelloClient'
import { SerialManagementConnection } from './device/serialManagementConnection'
import {
  loadMbedTlsPairingCryptoProvider,
  type PairingCryptoProvider,
} from './device/pairingCryptoProvider'
import { PhysicalDeviceTransport } from './device/physicalDeviceTransport'

const simulatedTransport = new SimulatedDeviceTransport()

export function App() {
  const [activeSection, setActiveSection] = useState('概览')
  const [deviceStatus, setDeviceStatus] = useState<DeviceStatus | null>(null)
  const [transport, setTransport] = useState<DeviceTransport>(simulatedTransport)
  const [connectionState, setConnectionState] = useState<'idle' | 'connecting' | 'pairing' | 'ready'>('idle')
  const [pairingCode, setPairingCode] = useState('')
  const [connectionError, setConnectionError] = useState('')
  const connection = useRef<SerialManagementConnection | null>(null)
  const cryptoProvider = useRef<PairingCryptoProvider | null>(null)
  const connectionAttempt = useRef(0)
  const mounted = useRef(true)

  useEffect(() => {
    mounted.current = true
    return () => {
      mounted.current = false
      connectionAttempt.current += 1
      void connection.current?.close()
    }
  }, [])

  async function connectPhysicalDevice() {
    const serial = browserSerialApi()
    if (serial === null) {
      setConnectionError('当前浏览器不支持 Web Serial，请使用桌面版 Chromium。')
      return
    }
    setConnectionError('')
    setConnectionState('connecting')
    const attempt = connectionAttempt.current + 1
    connectionAttempt.current = attempt
    try {
      // requestPort must run before any await so the click's user activation is retained.
      const selectedPort = await serial.requestPort()
      await connection.current?.close()
      connection.current = null
      cryptoProvider.current = null
      const provider = await loadMbedTlsPairingCryptoProvider()
      const nextConnection = await SerialManagementConnection.connectPort(selectedPort)
      if (!mounted.current || connectionAttempt.current !== attempt) {
        await nextConnection.close()
        return
      }
      connection.current = nextConnection
      cryptoProvider.current = provider
      setConnectionState('pairing')
    } catch {
      if (!mounted.current || connectionAttempt.current !== attempt) return
      setConnectionState('idle')
      setConnectionError('无法连接设备，请确认 USB 数据线和串口权限。')
    }
  }

  async function submitPairing(event: FormEvent<HTMLFormElement>) {
    event.preventDefault()
    const activeConnection = connection.current
    const provider = cryptoProvider.current
    if (activeConnection === null || provider === null) return
    setConnectionError('')
    try {
      await activeConnection.pair(pairingCode, provider)
      setPairingCode('')
      setTransport(new PhysicalDeviceTransport(activeConnection))
      setConnectionState('ready')
      setActiveSection('概览')
    } catch (error) {
      setPairingCode('')
      setConnectionError(error instanceof Error ? error.message : '安全配对失败')
      if (activeConnection.isClosed) {
        connection.current = null
        cryptoProvider.current = null
        setTransport(simulatedTransport)
        setConnectionState('idle')
      }
    }
  }

  async function useSimulator() {
    connectionAttempt.current += 1
    await connection.current?.close()
    connection.current = null
    cryptoProvider.current = null
    setTransport(simulatedTransport)
    setConnectionState('idle')
    setConnectionError('')
    setPairingCode('')
  }

  return (
    <main className="app-shell">
      <header className="topbar">
        <div className="wordmark">
          <span className="wordmark-orbit" aria-hidden="true" />
          <div>
            <strong>AIQA</strong>
            <span>DEVICE CONSOLE</span>
          </div>
        </div>
        <div className="connection-pill">
          <i /> {transport.mode === 'physical' ? '真机安全会话已连接' : '模拟设备已连接'}
        </div>
      </header>

      <section className="workspace">
        <aside className="device-stage">
          <div className="device-meta">
            <span>ESP32-S3 · 466 × 466</span>
            <span className="live-indicator">LIVE</span>
          </div>
          <div className="screen-orbit">
            <div className="device-screen" aria-label="设备界面预览">
              <div className="pet-face" aria-hidden="true">
                <span className="ear left" /><span className="ear right" />
                <span className="eye left" /><span className="eye right" />
                <span className="mouth" />
              </div>
              <p>{deviceStatus?.ui.status ?? 'SYNC'}</p>
              <small>{deviceStatus?.ui.hint ?? 'WAIT'}</small>
            </div>
          </div>
          <div className="runtime-strip">
            <div><span>STATE</span><strong>{deviceStatus?.state ?? '—'}</strong></div>
            <div><span>WI-FI</span><strong>{deviceStatus?.wifi.rssiDbm ?? '—'} dBm</strong></div>
            <div><span>HEAP</span><strong>{deviceStatus ? Math.round(deviceStatus.freeHeapBytes / 1024) : '—'} KB</strong></div>
          </div>
        </aside>

        <section className="control-panel">
          <div className="device-connection">
            {connectionState === 'pairing' ? (
              <form onSubmit={submitPairing}>
                <span>短按设备 BOOT 三次，然后输入屏幕上的 8 位码</span>
                <input
                  aria-label="设备配对码"
                  autoComplete="one-time-code"
                  inputMode="numeric"
                  maxLength={8}
                  pattern="[0-9]{8}"
                  required
                  type="password"
                  value={pairingCode}
                  onChange={(event) => setPairingCode(event.target.value.replace(/\D/g, ''))}
                />
                <button type="submit">建立加密会话</button>
              </form>
            ) : transport.mode === 'physical' ? (
              <button onClick={() => void useSimulator()} type="button">断开真机并使用模拟器</button>
            ) : (
              <button
                disabled={connectionState === 'connecting'}
                onClick={() => void connectPhysicalDevice()}
                type="button"
              >
                {connectionState === 'connecting' ? '正在连接…' : '连接真实设备'}
              </button>
            )}
            {connectionError ? <p role="alert">{connectionError}</p> : null}
          </div>
          <nav aria-label="设备设置">
            {['概览', '网络', '模型', '形象', 'Prompt', '调试'].map((section) => (
              <button
                className={activeSection === section ? 'active' : ''}
                key={section}
                onClick={() => setActiveSection(section)}
                type="button"
              >
                {section}
              </button>
            ))}
          </nav>
          <div className="panel-body">
            {activeSection === '概览' ? (
              <DeviceOverview onStatus={setDeviceStatus} transport={transport} />
            ) : activeSection === '网络' ? (
              <WifiSettings transport={transport} />
            ) : (
              <div className="coming-soon">
                <p className="eyebrow">IMPLEMENTATION QUEUE</p>
                <h2>{activeSection}</h2>
                <p>这一模块将在后续纵切接入同一设备管理协议。</p>
              </div>
            )}
          </div>
        </section>
      </section>
    </main>
  )
}
