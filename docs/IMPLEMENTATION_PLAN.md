# AI Pet Implementation Plan

## Runtime Contract

The application is event-driven. Only `app_state_task` owns the state machine.
Other tasks send events or receive commands through queues.

- `app_state_task`: state transitions, cancellation, error handling.
- `ui_task`: display owner for the circular pet interface; it must not perform network or I2S.
- `audio_task`: ES7210/I2S capture, PSRAM PCM buffer, max-duration guard, ASR handoff.
- `net_task`: Wi-Fi/config-transaction owner; serializes connect, reconfiguration,
  factory reset, SNTP, and network recovery.
- `provider` adapters: request/response protocol only; no UI or socket ownership.

## Phase Order

1. Project skeleton, events, fake providers, diagnostics.
2. Hardware bring-up: I2C scan, PMU, BOOT/PWR behavior, AMOLED init, ES7210, ES8311/PA.
3. Secure provisioning: NVS schema, key redaction, factory reset, cert bundle, SNTP.
4. Fixed-text Qwen call using `dashscope_openai_chat` and `qwen3.7-max`.
5. Bounded push-to-talk recording with 24 kHz/16-bit mono extraction.
6. ASR provider selection and recorded WAV data URI transcription.
7. End-to-end PTT: recording -> ASR -> chat answer -> pet response screen.
8. MiniMax chat adapter and provider capability table.
9. Soak tests, low-memory handling, rate limits, privacy UX, release hardening.

## Hardware Constants

- AMOLED: 466x466
- LCD SDIO0..3: GPIO 4/5/6/7
- LCD SCLK/CS/RESET: GPIO 38/12/1
- I2C SDA/SCL: GPIO 15/14
- Touch INT/RST: GPIO 11/2
- ES7210 BCLK/LRCK/DIN/MCLK: GPIO 9/45/10/16
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
  - 24 kHz
  - 16-bit
  - mono
  - 20 second maximum
  - 960 KB maximum PCM buffer
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
  - `asr_provider`
  - `asr_model`
  - `asr_base_url`
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
- Chat and ASR provider/model/base URL are stored independently, with legacy
  NVS partitions falling back to default Qwen ASR values when `asr_*` keys are
  absent.
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

## Current Device Management Scope

Implemented:

- React/Vite management console with a simulated transport, live overview, device
  screen preview, redacted model/key state, and Wi-Fi account/password editing.
- Strict JSON/Zod status contract for runtime, heap, Wi-Fi, battery, current UI,
  redacted model configuration, and latest asynchronous operation.
- Transport-independent firmware `management_service` with explicit public DTO
  allowlists; passwords, API keys, and base URLs are never returned.
- Session authorization is delegated to a trusted server-side callback. Runtime
  authorization currently fails closed until the pairing/authenticated transport
  phase supplies a verified session registry; wire payloads cannot set permission
  booleans.
- Wi-Fi changes return an operation ID and run asynchronously through the single
  `net_task` owner. The client transport abstraction must poll `latestOperation`
  before returning the refreshed public Wi-Fi view.
- Wi-Fi credentials are projected into the minimum credential-only type and are
  stored in owned heap jobs; FreeRTOS queues contain pointers rather than secret
  bytes. Every completion, cancellation, and queue-drain path clears memory.
- Atomic A/B NVS update flow: revision check, stage, verify, trial connection,
  activation, rollback, and recovery-required quarantine.
- Factory reset is queued behind any active network/config transaction, then
  disconnects Wi-Fi, clears the runtime snapshot, erases all configuration
  namespaces, and publishes the final result.
- Host tests include a real concurrent slow-reader/completion case. Management
  service line coverage is above 90%; client line coverage is above 95%.
- USB Serial/JTAG is now reserved for the management channel; firmware logs stay
  on UART0 so log bytes cannot corrupt management frames.
- Cross-platform `AQMG` v1 framing uses a fixed 12-byte big-endian header and a
  4096-byte payload ceiling. The incremental C and TypeScript decoders cover
  fragmented, coalesced, noisy, and oversized input.
- The first physical transport gate only exposes strict `system.hello`. It
  rejects unknown fields, embedded NUL bytes, malformed IDs, and every sensitive
  operation until an authenticated encrypted session exists.
- The USB owner task keeps the decoder off the task stack, clears temporary
  buffers, caps JSON depth, validates escapes before cJSON, rate-limits all
  inbound bytes, samples malformed-input logs, and never logs untrusted payloads.
- The browser Web Serial adapter can select a device, exchange and validate the
  framed hello response, enforce a timeout, and close stalled connections without
  displaying device-supplied error details.

Next:

- Implement an audited PAKE (MbedTLS ECJPAKE) followed by HKDF-derived AEAD session
  keys, monotonic replay counters, expiry, lockout, and a reset path. Do not open
  status, Wi-Fi, provider, key, prompt, or debug methods before this boundary.
- Add the authenticated operation polling adapter, then optionally reuse the same
  encrypted protocol over BLE/Wi-Fi.
- Before any production release, enable Secure Boot v2 plus release-mode Flash/NVS
  encryption and run a separate, reviewed eFuse procedure for USB/Pad JTAG and ROM
  download restrictions. Development boards must use a separate reversible profile;
  the application-level USB protocol alone is not a hardware security boundary.
- Add provider/model/key, assistant image, prompt, and simulation/debug write DTOs
  on top of the same transaction and authorization boundary.

## Current Chat Scope

Implemented:

- `chat_client` component for one-shot OpenAI-compatible chat requests.
- DashScope/Qwen endpoint construction:
  - configured base URL plus `/chat/completions`
  - default model `qwen3.7-max`
  - non-streaming fixed-text bring-up request
- Request JSON builder that keeps Authorization secrets out of request-body
  tests and logs.
- Provider-specific chat request options:
- DashScope/Qwen uses `max_tokens` and `enable_thinking`.
- Runtime clamps voice chat replies to 256 completion tokens to reduce
  end-to-end spoken-response latency.
  - MiniMax uses `max_completion_tokens` and `reasoning_split`.
- ESP-IDF `esp_http_client` transport with certificate bundle attachment.
- HTTP/auth/rate-limit/timeout/provider failures mapped back into runtime
  events.
- Chat prompt queue accepts text derived from the latest ASR transcript.
- Voice language switch commands are handled locally from ASR transcripts:
  phrases such as `使用中文与我交流` or `please speak English with me` switch
  between Chinese and English, play a local confirmation, and pass `zh`/`en`
  response-language hints into later chat requests.
- Short-term conversation memory keeps the latest 3 successful user/pet turns
  in RAM and injects them as chat context before the next user message.
- State machine accepts chat start from `IDLE` and `IDLE_WITH_RESULT`.
- State machine also accepts chat start while already `THINKING`, avoiding a
  race warning when the chat worker begins immediately after ASR completion.
- Host contract tests for request formatting, response parsing, and state
  transitions.
- Host contract tests cover MiniMax request formatting without Qwen-only
  fields.

Not yet implemented:

- Streaming chat token display.
- Device-side verified live Qwen call with provisioned Wi-Fi/API credentials.

## Current Pet UI Scope

Implemented:

- Centered Codex-inspired 24x24 procedural pixel pet sprite abstraction.
- Circular-safe sprite layout contract for the 1.75C round AMOLED safe area.
- Multi-frame pet animation for idle, listening, thinking, speaking, and
  emotion states.
- Scene/emotion mapping for happy, sad, shy, frustrated, bouncing, laughing,
  crying, curious, worried, and sleepy expressions.
- Runtime UI redraw ticker refreshes only the pet sprite region while waiting
  in a stable state, keeping audio/chat tasks ahead of LCD animation work.
- Dialogue view keeps a compact pet emotion hint from raw Chinese or English
  replies, so Chinese text can still drive pet expressions even when the round
  screen fallback text remains ASCII.
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
- ES7210 capture bring-up using `esp_codec_dev` and ESP-IDF I2S TDM RX.
- PSRAM-backed PCM recording buffer for 24 kHz/16-bit/mono audio.
- Runtime PCM diagnostics: captured bytes, mono sample count, PCM bytes, and peak level.
- Recorded PCM handoff to ASR as a WAV data URI after PTT release.
- Startup ES8311+PA playback self-test tone after Wi-Fi reaches `IDLE`.
- Startup online Qwen-TTS self-test is skipped so the first user interaction
  does not wait behind a synthetic speech request.
- Qwen-TTS streaming parser now accepts large SSE audio chunks and treats the
  final empty `audio.data` URL frame as a normal stream ending.
- Qwen-TTS PCM chunks are split into a fixed 1 KB slot pool and sent to a
  dedicated playback task through pointer-sized FreeRTOS queue items as SSE
  audio arrives, so ES8311 playback can start before the full TTS response has
  finished downloading while keeping queued PCM memory and task stack usage
  bounded. The slot pool is sized for roughly two seconds of 24 kHz/16-bit mono
  PCM jitter and prefers PSRAM allocation.
- Playback waits briefly for an initial jitter buffer before starting, so
  provider-side SSE burst gaps are less likely to become audible pauses.
- The playback queue reserves one extra pointer slot for the stream-ending
  marker so normal queued audio cannot block completion signaling.
- Playback-side cancellation or queue backpressure can abort the active TTS
  HTTP stream instead of continuing to decode audio that can no longer be
  played.
- Host contract tests cover short-press rejection, long-press start, release
  stop, and one-shot timeout behavior.

Not yet implemented:

- Touch/PWR-button PTT as the production input.
- On-device playback or local persistence of captured audio.

## Current ASR Scope

Implemented:

- `asr_client` component for one-shot OpenAI-compatible Qwen ASR requests.
- DashScope ASR endpoint construction:
  - configured ASR base URL plus `/chat/completions`
  - default model `qwen3-asr-flash`
- Request JSON builder using `input_audio.data`, language hint, and ITN option.
- Static public audio URL request kept as a fallback/contract path.
- Streaming HTTP writer for in-memory WAV data URI requests so the full base64
  request does not need to exist as one large JSON buffer.
- WAV header generation and provider audio-size validation.
- ESP-IDF `esp_http_client` transport with certificate bundle attachment.
- HTTP/auth/rate-limit/timeout/provider failures mapped back into runtime
  events.
- Runtime `asr_task` receives the completed PCM recording after PTT release and
  posts `ASR_DONE` or failure.
- Latest transcript is retained in runtime memory long enough to hand it to the
  chat worker.
- Host contract tests for ASR request formatting, response parsing, provider
  config separation, WAV header/data URI construction, and provisioning CSV
  output.

Not yet implemented:

- MiniMax or alternate ASR providers.

## Current End-To-End PTT Scope

Implemented:

- BOOT long press -> `RECORDING`.
- BOOT release -> `TRANSCRIBING`.
- Captured ES7210 PCM -> WAV data URI -> Qwen ASR; transcript length logged,
  text content kept out of logs.
- ASR success -> `THINKING`.
- Latest ASR transcript -> Qwen chat request.
- Chat success -> `IDLE_WITH_RESULT`.
- A new BOOT long press while ASR/chat/TTS is still pending starts a new
  interaction generation, cancels active HTTP requests, drops stale results,
  and records the next prompt immediately.
- Contract test covers `ASR_STARTED -> ASR_DONE -> CHAT_STARTED -> CHAT_DONE`.

Not yet implemented:

- Streaming token-by-token answer display.
- Rendering answer text on the circular screen.

## Current MiniMax Scope

Implemented:

- MiniMax chat provider remains independently selectable from Qwen ASR.
- Provisioning dry-run and NVS paths can keep MiniMax chat plus default Qwen
  ASR in the same device config.
- Chat protocol emits MiniMax-compatible request fields and excludes
  Qwen-only `enable_thinking`.

Not yet implemented:

- Live MiniMax device call with a MiniMax API key.
- MiniMax-specific response metadata handling beyond OpenAI-compatible
  `message.content`.

## Current Hardening Scope

Implemented:

- State-machine soak contract runs 200 repeated PTT/ASR/chat cycles.
- Release hardening policy contract defines:
  - minimum free heap before model requests
  - default provider rate-limit cooldown
  - maximum consecutive provider failures
  - transcript and answer redaction defaults
- Host contract tests verify heap gating and cooldown calculations.
- Captured ES7210 PCM is wrapped as an in-memory WAV for ASR.
- Chat streaming now parses SSE `delta.content` chunks and redraws the pet
  dialogue page while the model is still answering.

Not yet implemented:

- Runtime heap gate enforcement before each ASR/chat HTTP request.
- Runtime cooldown persistence across reboot.
- Long-running on-device soak with Wi-Fi and provider APIs.

Not yet implemented:

- AXP2101 register reads / PWR event decoding.
- ES8311 playback and PA pop suppression sequence.
- Production secret hardening with ESP-IDF NVS encryption:
  - flash encryption plus `nvs_keys`, or
  - HMAC eFuse key derivation.
