import { StrictMode } from 'react'
import { render, screen } from '@testing-library/react'
import userEvent from '@testing-library/user-event'
import { afterEach, describe, expect, it, vi } from 'vitest'

import { App } from './App'

const mocks = vi.hoisted(() => ({
  close: vi.fn(async () => undefined),
  connectPort: vi.fn(),
  loadProvider: vi.fn(),
  requestPort: vi.fn(),
}))

vi.mock('./device/pairingCryptoProvider', () => ({
  loadMbedTlsPairingCryptoProvider: mocks.loadProvider,
}))

vi.mock('./device/serialManagementConnection', () => ({
  SerialManagementConnection: {
    connectPort: mocks.connectPort,
  },
}))

afterEach(() => {
  vi.restoreAllMocks()
  delete (navigator as Navigator & { serial?: unknown }).serial
})

describe('App physical connection', () => {
  it('retains the selected connection across StrictMode effect replay', async () => {
    const port = { marker: 'selected-port' }
    const connection = {
      close: mocks.close,
      isClosed: false,
      pair: vi.fn(),
      requestSecure: vi.fn(),
    }
    mocks.requestPort.mockResolvedValue(port)
    mocks.loadProvider.mockResolvedValue({ createSession: vi.fn() })
    mocks.connectPort.mockResolvedValue(connection)
    Object.defineProperty(navigator, 'serial', {
      configurable: true,
      value: { requestPort: mocks.requestPort },
    })

    render(
      <StrictMode>
        <App />
      </StrictMode>,
    )
    await userEvent.click(screen.getByRole('button', { name: '连接真实设备' }))

    expect(await screen.findByLabelText('设备配对码')).toBeInTheDocument()
    expect(mocks.requestPort).toHaveBeenCalledOnce()
    expect(mocks.connectPort).toHaveBeenCalledWith(port)
    expect(mocks.close).not.toHaveBeenCalled()
  })
})
