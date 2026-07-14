# AIQA Device Console

The first implementation slice provides the device-console shell, a safe Wi-Fi
configuration form, revision conflict handling, and a simulated device
transport. Passwords are write-only: public configuration contains only the
SSID and the `hasPassword` flag.

Run locally:

    corepack pnpm install
    corepack pnpm dev

Quality checks:

    corepack pnpm test
    corepack pnpm test:coverage
    corepack pnpm build

The current UI uses `SimulatedDeviceTransport`. The next slice will implement
the A/B NVS configuration manager and bind the same `DeviceTransport` contract
to the physical USB management channel.
