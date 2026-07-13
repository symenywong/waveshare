# AI Pet Implementation Plan

## Runtime Contract

The application is event-driven. Only `app_state_task` owns the state machine.
Other tasks send events or receive commands through queues.

- `app_state_task`: state transitions, cancellation, error handling.
- `ui_task`: display owner for the circular pet interface; it must not perform network or I2S.
- `audio_task`: ES7210/I2S capture, PSRAM PCM buffer, max-duration guard, ASR handoff.
- `net_task`: future HTTPS owner with TLS validation, staged timeouts, streaming.
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
- Startup Qwen-TTS online self-test routed through the same playback path as
  pet replies.
- Qwen-TTS streaming parser now accepts large SSE audio chunks and treats the
  final empty `audio.data` URL frame as a normal stream ending.
- Qwen-TTS PCM is buffered in PSRAM before ES8311 playback, avoiding playback
  gaps caused by variable online SSE chunk arrival timing.
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
