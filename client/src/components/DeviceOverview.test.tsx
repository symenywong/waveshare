import { render, screen } from '@testing-library/react'
import userEvent from '@testing-library/user-event'
import { describe, expect, it, vi } from 'vitest'

import { DeviceOverview } from './DeviceOverview'
import type { DeviceTransport } from '../device/deviceTransport'

function makeTransport(): DeviceTransport {
  return {
    mode: 'simulated',
    getDeviceStatus: vi.fn().mockResolvedValue({
      sequence: 7,
      state: 'IDLE',
      error: 'NONE',
      uptimeMs: 65_432,
      freeHeapBytes: 188_416,
      wifi: { connected: true, rssiDbm: -48 },
      power: { batteryPresent: true, percent: 82, chargingState: 'DISCHARGING' },
      ui: { status: 'READY', detail: null, hint: 'HOLD BOOT', expression: 'IDLE' },
      config: {
        available: true,
        revision: 3,
        chatProvider: 'dashscope_openai_chat',
        chatModel: 'qwen3.7-max',
        hasChatApiKey: true,
        hasAsrApiKey: true,
      },
      latestOperation: { id: 2, state: 'SUCCEEDED', result: 'OK' },
    }),
    getWifiConfig: vi.fn(),
    updateWifi: vi.fn(),
  }
}

describe('DeviceOverview', () => {
  it('renders runtime, UI, network, power and redacted model status', async () => {
    render(<DeviceOverview transport={makeTransport()} />)

    expect(await screen.findByRole('heading', { name: '设备概览' })).toBeVisible()
    expect(screen.getByText('IDLE')).toBeVisible()
    expect(screen.getByText('READY')).toBeVisible()
    expect(screen.getByText('–48 dBm')).toBeVisible()
    expect(screen.getByText('82%')).toBeVisible()
    expect(screen.getByText('qwen3.7-max')).toBeVisible()
    expect(screen.getAllByText('已配置')).toHaveLength(2)
    expect(screen.queryByText('must-not-leak')).not.toBeInTheDocument()
  })

  it('shows a recoverable error when the status request fails', async () => {
    const transport = makeTransport()
    vi.mocked(transport.getDeviceStatus)
      .mockRejectedValueOnce(new Error('offline'))
      .mockResolvedValueOnce(await makeTransport().getDeviceStatus())
    const user = userEvent.setup()

    render(<DeviceOverview transport={transport} />)

    expect(await screen.findByRole('alert')).toHaveTextContent('无法读取设备状态')
    await user.click(screen.getByRole('button', { name: '重新读取' }))
    expect(await screen.findByRole('heading', { name: '设备概览' })).toBeVisible()
  })

  it('keeps the last snapshot and reports a refresh failure', async () => {
    const transport = makeTransport()
    const user = userEvent.setup()
    render(<DeviceOverview transport={transport} />)
    expect(await screen.findByText('LIVE SNAPSHOT / 7')).toBeVisible()

    vi.mocked(transport.getDeviceStatus).mockRejectedValueOnce(new Error('offline'))
    await user.click(screen.getByRole('button', { name: '刷新状态' }))

    expect(await screen.findByRole('alert')).toHaveTextContent('无法读取设备状态')
    expect(screen.getByText('LIVE SNAPSHOT / 7')).toBeVisible()
  })
})
