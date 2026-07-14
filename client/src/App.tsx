import { useState } from 'react'

import { WifiSettings } from './components/WifiSettings'
import { SimulatedDeviceTransport } from './device/deviceTransport'

const transport = new SimulatedDeviceTransport()

export function App() {
  const [activeSection, setActiveSection] = useState('网络')

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
        <div className="connection-pill"><i /> 模拟设备已连接</div>
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
              <p>READY</p>
              <small>HOLD BOOT</small>
            </div>
          </div>
          <div className="runtime-strip">
            <div><span>STATE</span><strong>IDLE</strong></div>
            <div><span>WI-FI</span><strong>–48 dBm</strong></div>
            <div><span>HEAP</span><strong>184 KB</strong></div>
          </div>
        </aside>

        <section className="control-panel">
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
            {activeSection === '网络' ? (
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
