import { render, screen } from '@testing-library/react'
import userEvent from '@testing-library/user-event'
import { describe, expect, it } from 'vitest'

import { WifiSettings } from './WifiSettings'
import { SimulatedDeviceTransport, type DeviceTransport } from '../device/deviceTransport'

describe('WifiSettings', () => {
  it('loads the current SSID and saves a replacement password', async () => {
    const user = userEvent.setup()
    const transport = new SimulatedDeviceTransport()
    render(<WifiSettings transport={transport} />)

    expect(await screen.findByDisplayValue('AIQA-Lab')).toBeVisible()
    await user.clear(screen.getByLabelText('Wi-Fi 名称'))
    await user.type(screen.getByLabelText('Wi-Fi 名称'), 'studio-network')
    await user.click(screen.getByLabelText('更换密码'))
    await user.type(screen.getByLabelText(/新密码/), 'correct-horse')
    await user.click(screen.getByRole('button', { name: '保存到模拟设备' }))

    expect(await screen.findByText('模拟 Wi-Fi 配置已保存，未写入真机')).toBeVisible()
    expect(screen.queryByDisplayValue('correct-horse')).not.toBeInTheDocument()
    await expect(transport.getWifiConfig()).resolves.toMatchObject({
      revision: 2,
      ssid: 'studio-network',
      hasPassword: true,
    })
  })

  it('shows validation feedback without sending an invalid password', async () => {
    const user = userEvent.setup()
    render(<WifiSettings transport={new SimulatedDeviceTransport()} />)

    expect(await screen.findByDisplayValue('AIQA-Lab')).toBeVisible()
    await user.click(screen.getByLabelText('更换密码'))
    await user.type(screen.getByLabelText(/新密码/), 'short')
    await user.click(screen.getByRole('button', { name: '保存到模拟设备' }))

    expect(await screen.findByRole('alert')).toHaveTextContent('8 到 63')
  })

  it('clears password state when leaving replace mode', async () => {
    const user = userEvent.setup()
    render(<WifiSettings transport={new SimulatedDeviceTransport()} />)

    expect(await screen.findByDisplayValue('AIQA-Lab')).toBeVisible()
    await user.click(screen.getByLabelText('更换密码'))
    await user.type(screen.getByLabelText(/新密码/), 'correct-horse')
    await user.click(screen.getByLabelText('清空密码，连接开放网络'))
    await user.click(screen.getByLabelText('更换密码'))

    expect(screen.getByLabelText(/新密码/)).toHaveValue('')
  })

  it('can explicitly clear an existing password for an open network', async () => {
    const user = userEvent.setup()
    const transport = new SimulatedDeviceTransport()
    render(<WifiSettings transport={transport} />)

    expect(await screen.findByDisplayValue('AIQA-Lab')).toBeVisible()
    await user.click(screen.getByLabelText('清空密码，连接开放网络'))
    await user.click(screen.getByRole('button', { name: '保存到模拟设备' }))

    expect(await screen.findByText('开放网络')).toBeVisible()
    await expect(transport.getWifiConfig()).resolves.toMatchObject({
      revision: 2,
      hasPassword: false,
    })

    await user.click(screen.getByLabelText('保留现有密码'))
    expect(screen.getByLabelText('保留现有密码')).toBeChecked()
  })

  it('shows a retry action when the initial device read fails', async () => {
    const transport: DeviceTransport = {
      mode: 'physical',
      getWifiConfig: async () => {
        throw new Error('transport details must not leak')
      },
      updateWifi: async () => {
        throw new Error('not reached')
      },
    }
    const user = userEvent.setup()
    render(<WifiSettings transport={transport} />)

    expect(await screen.findByRole('alert')).toHaveTextContent('无法读取设备 Wi-Fi 配置')
    await user.click(screen.getByRole('button', { name: '重新读取' }))
    expect(await screen.findByRole('alert')).not.toHaveTextContent('transport details')
  })

  it('reloads the latest device config after a revision conflict', async () => {
    const user = userEvent.setup()
    const transport = new SimulatedDeviceTransport()
    render(<WifiSettings transport={transport} />)
    expect(await screen.findByDisplayValue('AIQA-Lab')).toBeVisible()

    await transport.updateWifi({
      baseRevision: 1,
      ssid: 'externally-updated',
      passwordAction: 'keep',
    })
    await user.clear(screen.getByLabelText('Wi-Fi 名称'))
    await user.type(screen.getByLabelText('Wi-Fi 名称'), 'stale-local-edit')
    await user.click(screen.getByRole('button', { name: '保存到模拟设备' }))

    expect(await screen.findByRole('alert')).toHaveTextContent('已加载最新配置')
    expect(screen.getByDisplayValue('externally-updated')).toBeVisible()
  })

  it('does not render untrusted transport error details', async () => {
    const transport: DeviceTransport = {
      mode: 'physical',
      getWifiConfig: async () => ({ revision: 1, ssid: 'office', hasPassword: true }),
      updateWifi: async () => {
        throw new Error('request contained correct-horse')
      },
    }
    const user = userEvent.setup()
    render(<WifiSettings transport={transport} />)
    expect(await screen.findByDisplayValue('office')).toBeVisible()
    await user.click(screen.getByRole('button', { name: '保存并重新连接' }))

    expect(await screen.findByRole('alert')).toHaveTextContent('请检查输入和设备连接')
    expect(screen.getByRole('alert')).not.toHaveTextContent('correct-horse')
  })
})
