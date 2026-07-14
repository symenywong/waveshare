import { describe, expect, it } from 'vitest'

import {
  RevisionConflictError,
  RevisionExhaustedError,
  SimulatedDeviceTransport,
} from './deviceTransport'

describe('SimulatedDeviceTransport', () => {
  it('updates Wi-Fi without returning the password', async () => {
    const transport = new SimulatedDeviceTransport()

    const updated = await transport.updateWifi({
      baseRevision: 1,
      ssid: 'studio-network',
      passwordAction: 'replace',
      password: 'correct-horse',
    })

    expect(updated).toEqual({
      revision: 2,
      ssid: 'studio-network',
      hasPassword: true,
    })
    expect(JSON.stringify(updated)).not.toContain('correct-horse')
  })

  it('rejects stale revisions and preserves the active configuration', async () => {
    const transport = new SimulatedDeviceTransport()

    await expect(
      transport.updateWifi({
        baseRevision: 99,
        ssid: 'stale-network',
        passwordAction: 'clear',
      }),
    ).rejects.toBeInstanceOf(RevisionConflictError)

    await expect(transport.getWifiConfig()).resolves.toEqual({
      revision: 1,
      ssid: 'AIQA-Lab',
      hasPassword: true,
    })
  })

  it('supports preserving and clearing the existing password explicitly', async () => {
    const transport = new SimulatedDeviceTransport()
    const kept = await transport.updateWifi({
      baseRevision: 1,
      ssid: 'renamed-network',
      passwordAction: 'keep',
    })
    expect(kept.hasPassword).toBe(true)

    const cleared = await transport.updateWifi({
      baseRevision: kept.revision,
      ssid: 'guest-network',
      passwordAction: 'clear',
    })
    expect(cleared.hasPassword).toBe(false)
  })

  it('rejects revision overflow consistently with the firmware contract', async () => {
    const transport = new SimulatedDeviceTransport({
      revision: 4_294_967_295,
      ssid: 'maintenance-network',
      hasPassword: true,
    })

    await expect(
      transport.updateWifi({
        baseRevision: 4_294_967_295,
        ssid: 'next-network',
        passwordAction: 'keep',
      }),
    ).rejects.toBeInstanceOf(RevisionExhaustedError)
  })
})
