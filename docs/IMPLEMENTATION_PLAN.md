# AI Pet Implementation Plan

## Runtime Contract

The application is event-driven. Only `app_state_task` owns the state machine.
Other tasks send events or receive commands through queues.

- `app_state_task`: state transitions, cancellation, error handling.
- `ui_task`: display owner for the circular pet interface; it must not perform network or I2S.
- `audio_task`: future I2S DMA capture, PSRAM ring buffer, max-duration guard.
- `net_task`: future HTTPS owner with TLS validation, staged timeouts, streaming.
- `provider` adapters: request/response protocol only; no UI or socket ownership.

## Phase Order

1. Project skeleton, events, fake providers, diagnostics.
2. Hardware bring-up: I2C scan, PMU, BOOT/PWR behavior, AMOLED init, ES7210, ES8311/PA.
3. Secure provisioning: NVS schema, key redaction, factory reset, cert bundle, SNTP.
4. Fixed-text Qwen call using `dashscope_openai_chat` and `qwen3.7-max`.
5. Bounded push-to-talk recording with 16 kHz/16-bit mono extraction.
6. ASR provider selection and static audio sample transcription.
7. End-to-end PTT: recording -> ASR -> streamed chat answer -> pet response screen.
8. MiniMax chat adapter and provider capability table.
9. Soak tests, low-memory handling, rate limits, privacy UX, release hardening.

## Hardware Constants

- AMOLED: 466x466
- LCD SDIO0..3: GPIO 4/5/6/7
- LCD SCLK/CS/RESET: GPIO 38/12/39
- I2C SDA/SCL: GPIO 15/14
- Touch INT/RST: GPIO 11/40
- ES7210 BCLK/LRCK/DIN/MCLK: GPIO 9/45/10/42
- ES8311 DOUT: GPIO 8
- PA: GPIO 46

`PWR` must not be treated as a plain GPIO until AXP2101 behavior is verified.
`BOOT(GPIO0)` is development fallback only because it affects boot mode.

## Current Bring-Up Scope

Implemented:

- Initialize BOOT as input with pull-up.
- Initialize PA as output and force safe-off at boot.
- Initialize ESP-IDF I2C master on `SDA=15/SCL=14`.
- Scan I2C addresses `0x08..0x77`.
- Mark required PTT devices as present/missing:
  - AXP2101 `0x34`
  - ES7210 `0x40`
  - ES8311 `0x18`
- Track optional touch and IMU addresses:
  - CST9217 `0x5A`
  - QMI8658 `0x6B`
- Define safe audio capture budget:
  - 16 kHz
  - 16-bit
  - mono
  - 20 second maximum
  - 640 KB maximum PCM buffer
- Initialize the CO5300 AMOLED over QSPI and render the current runtime state.
- Render the first AI pet status surface:
  - circular-safe centered text
  - non-card pet face composition
  - sleepy, curious, happy, listening, thinking, and worried expressions
  - warm warning color for setup/errors instead of a red failure screen
- ESP-IDF build/flash validated on `/dev/cu.usbmodem1101`.
- Boot log confirms all expected I2C devices are visible:
  - AXP2101 `0x34`
  - ES7210 `0x40`
  - ES8311 `0x18`
  - CST9217 `0x5A`
  - QMI8658 `0x6B`

## Current Provisioning Scope

Implemented:

- Provider/model/base URL allowlists in Python and device-side C.
- NVS namespace schema `aiqa`.
- NVS keys:
  - `version`
  - `provider`
  - `model`
  - `base_url`
  - `stream`
  - `hide_reason`
  - `max_tokens`
  - `wifi_ssid`
  - `wifi_pass`
  - `chat_key`
  - `asr_key`
- Provisioning CLI that rejects command-line API keys.
- Redacted stdout for API keys.
- Development-only plaintext NVS CSV output gated by `--allow-plaintext-nvs`.
- Sensitive NVS CSV output created atomically with `0600` permissions.
- `asr_key` is currently populated from the chat API key by the provisioning
  helper; a separate ASR key can be added when provider mix requires it.
- Runtime NVS loading and validation.
- Runtime state now reports:
  - `CONFIG_MISSING` when `aiqa` namespace is absent.
  - `CONFIG_CORRUPT` when provider, URL, Wi-Fi, or key validation fails.

## Current Network Scope

Implemented:

- `net_connect` component with Wi-Fi station connection ownership.
- Wi-Fi credentials are copied to ESP-IDF Wi-Fi config from the validated NVS
  snapshot and are not logged.
- ESP-IDF Wi-Fi storage is set to RAM to avoid duplicating credentials into
  the default Wi-Fi NVS area.
- SNTP synchronization via `esp_netif_sntp`.
- Network state now flows through `net_task`:
  - `CONFIG_READY` -> `NETWORK_CONNECTING`
  - Wi-Fi + SNTP success -> `NETWORK_READY`
  - Wi-Fi/SNTP failure -> `NETWORK_FAILED`
- Network policy contract tests cover retry delay and minimum valid TLS time.

## Current Chat Scope

Implemented:

- `chat_client` component for one-shot OpenAI-compatible chat requests.
- DashScope/Qwen endpoint construction:
  - configured base URL plus `/chat/completions`
  - default model `qwen3.7-max`
  - non-streaming fixed-text bring-up request
- Request JSON builder that keeps Authorization secrets out of request-body
  tests and logs.
- ESP-IDF `esp_http_client` transport with certificate bundle attachment.
- HTTP/auth/rate-limit/timeout/provider failures mapped back into runtime
  events.
- Automatic first fixed prompt after `NETWORK_READY`.
- State machine accepts chat start from `IDLE` and `IDLE_WITH_RESULT`.
- Host contract tests for request formatting, response parsing, and state
  transitions.

Not yet implemented:

- Streaming chat token display.
- Real ASR-derived prompts.
- Separate MiniMax chat adapter.
- Device-side verified live Qwen call with provisioned Wi-Fi/API credentials.

## Current Pet UI Scope

Implemented:

- Centered 16x16 pixel pet sprite abstraction.
- Circular-safe sprite layout contract.
- Temporary local pixel sprites for happy, listening, thinking, and worried
  states.
- AMOLED page rendering now uses the sprite pet instead of geometric circle
  shapes, keeping text and pet content away from the circular screen edge.

## Current PTT Recording Scope

Implemented:

- Pure C BOOT long-press policy:
  - 40 ms debounce
  - 500 ms long-press threshold
  - 20 ms polling cadence
  - 20 second maximum recording session
- Runtime `button_task` polling BOOT as the temporary development PTT input.
- `PRESS_START`, `PRESS_END`, and `AUDIO_TOO_LONG` event emission.
- `audio_task` owns recording start/stop commands and logs the capture session
  lifecycle.
- Host contract tests cover short-press rejection, long-press start, release
  stop, and one-shot timeout behavior.

Not yet implemented:

- Real ES7210 codec initialization.
- I2S DMA PCM capture into PSRAM.
- Passing recorded PCM bytes into ASR.

Not yet implemented:

- AXP2101 register reads / PWR event decoding.
- ES7210 codec initialization and I2S DMA capture.
- ES8311 playback and PA pop suppression sequence.
- ASR calls.
- Animated pet mood transitions and richer response presentation.
- Production secret hardening with ESP-IDF NVS encryption:
  - flash encryption plus `nvs_keys`, or
  - HMAC eFuse key derivation.
