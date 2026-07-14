import { useCallback, useEffect, useState, type FormEvent } from 'react'

import { RevisionConflictError, type DeviceTransport } from '../device/deviceTransport'
import {
  parseWifiUpdate,
  WifiValidationError,
  type WifiPublicConfig,
} from '../device/wifiConfig'

interface WifiSettingsProps {
  readonly transport: DeviceTransport
}

type PasswordAction = 'keep' | 'replace' | 'clear'

export function WifiSettings({ transport }: WifiSettingsProps) {
  const [config, setConfig] = useState<WifiPublicConfig | null>(null)
  const [ssid, setSsid] = useState('')
  const [passwordAction, setPasswordAction] = useState<PasswordAction>('keep')
  const [password, setPassword] = useState('')
  const [message, setMessage] = useState('')
  const [error, setError] = useState('')
  const [loadError, setLoadError] = useState('')
  const [isSaving, setIsSaving] = useState(false)

  const loadConfig = useCallback(async () => {
    setLoadError('')
    try {
      const nextConfig = await transport.getWifiConfig()
      setConfig(nextConfig)
      setSsid(nextConfig.ssid)
    } catch {
      setLoadError('无法读取设备 Wi-Fi 配置')
    }
  }, [transport])

  useEffect(() => {
    let isActive = true
    void (async () => {
      try {
        const nextConfig = await transport.getWifiConfig()
        if (isActive) {
          setConfig(nextConfig)
          setSsid(nextConfig.ssid)
        }
      } catch {
        if (isActive) {
          setLoadError('无法读取设备 Wi-Fi 配置')
        }
      }
    })()
    return () => {
      isActive = false
    }
  }, [transport])

  async function handleSubmit(event: FormEvent<HTMLFormElement>) {
    event.preventDefault()
    if (config === null) {
      return
    }
    setError('')
    setMessage('')

    try {
      const update = parseWifiUpdate({
        baseRevision: config.revision,
        ssid,
        passwordAction,
        ...(passwordAction === 'replace' ? { password } : {}),
      })
      setIsSaving(true)
      const nextConfig = await transport.updateWifi(update)
      setConfig(nextConfig)
      setPassword('')
      setPasswordAction(nextConfig.hasPassword ? 'keep' : 'clear')
      setMessage(
        transport.mode === 'simulated'
          ? '模拟 Wi-Fi 配置已保存，未写入真机'
          : 'Wi-Fi 配置已保存',
      )
    } catch (caught) {
      if (caught instanceof RevisionConflictError) {
        setPassword('')
        try {
          const latestConfig = await transport.getWifiConfig()
          setConfig(latestConfig)
          setSsid(latestConfig.ssid)
          setError(caught.message + '，已加载最新配置')
        } catch {
          setError(caught.message)
        }
      } else if (caught instanceof WifiValidationError) {
        setError(caught.message)
      } else {
        setError('Wi-Fi 配置保存失败，请检查输入和设备连接')
      }
    } finally {
      setIsSaving(false)
    }
  }

  if (config === null) {
    if (loadError) {
      return (
        <div className="load-error" role="alert">
          <p>{loadError}</p>
          <button onClick={() => void loadConfig()} type="button">重新读取</button>
        </div>
      )
    }
    return <div className="loading-line">正在读取设备配置…</div>
  }

  return (
    <form className="wifi-form" onSubmit={handleSubmit}>
      <div className="section-heading">
        <div>
          <p className="eyebrow">NETWORK / REV {config.revision}</p>
          <h2>Wi-Fi 设置</h2>
        </div>
        <span className={`credential-state ${config.hasPassword ? 'secured' : 'open'}`}>
          {config.hasPassword ? '已设置密码' : '开放网络'}
        </span>
      </div>

      {transport.mode === 'simulated' ? (
        <p className="simulation-notice">当前为模拟设备，保存操作不会修改真实硬件。</p>
      ) : null}

      <label className="field">
        <span>Wi-Fi 名称</span>
        <input
          autoComplete="off"
          name="ssid"
          value={ssid}
          onChange={(event) => setSsid(event.target.value)}
        />
      </label>

      <fieldset className="password-options">
        <legend>密码处理</legend>
        <label>
          <input
            checked={passwordAction === 'keep'}
            name="password-action"
            onChange={() => {
              setPassword('')
              setPasswordAction('keep')
            }}
            type="radio"
          />
          <span>保留现有密码</span>
        </label>
        <label>
          <input
            checked={passwordAction === 'replace'}
            name="password-action"
            onChange={() => setPasswordAction('replace')}
            type="radio"
          />
          <span>更换密码</span>
        </label>
        <label>
          <input
            checked={passwordAction === 'clear'}
            name="password-action"
            onChange={() => {
              setPassword('')
              setPasswordAction('clear')
            }}
            type="radio"
          />
          <span>清空密码，连接开放网络</span>
        </label>
      </fieldset>

      {passwordAction === 'replace' ? (
        <label className="field">
          <span>新密码</span>
          <input
            autoComplete="off"
            name="wifi-password"
            type="password"
            value={password}
            onChange={(event) => setPassword(event.target.value)}
          />
          <small>密码只会发送给设备，不会出现在配置响应或日志中。</small>
        </label>
      ) : null}

      {error ? <p role="alert" className="form-message error">{error}</p> : null}
      {message ? <p role="status" className="form-message success">{message}</p> : null}

      <button className="primary-action" disabled={isSaving} type="submit">
        {isSaving
          ? '正在验证网络…'
          : transport.mode === 'simulated'
            ? '保存到模拟设备'
            : '保存并重新连接'}
      </button>
    </form>
  )
}
