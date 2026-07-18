# AIQA Device Console

The current checkpoint provides the first physical device-management vertical
slice. It supports both a simulated device and an ESP32-S3 connected through
Chromium Web Serial.

Implemented:

- USB device selection and strict `system.hello` negotiation.
- Local-presence pairing after three short BOOT presses and entry of the
  eight-digit code displayed by the device.
- ECJPAKE pairing, Finished confirmation, and encrypted AES-GCM management
  requests through the bundled MbedTLS WASM boundary with integrity verification.
- Authenticated device status and public-configuration reads.
- Wi-Fi SSID/password updates with revision conflict handling and asynchronous
  result polling.
- A simulator for the same status and Wi-Fi UI.

Wi-Fi passwords are write-only. Future API-key write operations must follow the same
replace/clear-only rule. Public responses expose only the Wi-Fi SSID and boolean
`hasPassword`/`has*ApiKey` indicators.

Run locally:

    corepack pnpm install
    corepack pnpm dev

Use `localhost` or an HTTPS origin. Web Serial is available only in a compatible
desktop Chromium browser and requires a user gesture when selecting the device.

Quality checks:

    corepack pnpm test
    corepack pnpm test:coverage
    corepack pnpm build

Implemented scope pending repeatable physical acceptance:

1. Connect from a desktop Chromium browser.
2. Press BOOT three times, wait for the pairing code, and establish the secure
   session.
3. Read the device overview.
4. Read or update Wi-Fi configuration and observe the final operation result.

Not implemented at this checkpoint:

- Provider/model/TTS voice and API-key writes.
- Prompt editing.
- Animal-image selection or upload.
- Advanced simulation/debug scenarios.
- Pixel-accurate framebuffer mirroring.
- Automatic reconnect, browser E2E coverage, desktop packaging, or deployment.

Known limitations:

- Web Serial requires a compatible desktop Chromium browser and a user gesture
  for device selection.
- The screen preview is a logical status preview, not a streamed copy of the
  physical 466x466 display.
- Physical disconnect/session-expiry state and the 60-second Wi-Fi operation
  timeout boundary must be hardened before release.

The detailed pause checkpoint, remaining phases, estimates, and release risks
are recorded in `../docs/IMPLEMENTATION_PLAN.md`.
