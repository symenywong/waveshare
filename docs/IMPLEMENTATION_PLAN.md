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

- React/Vite management console with simulated and physical Web Serial transports,
  live overview, logical device-screen preview, redacted model/key state, and Wi-Fi
  account/password editing.
- Strict JSON/Zod status contract for runtime, heap, Wi-Fi, battery, current UI,
  redacted model configuration, and latest asynchronous operation.
- Transport-independent firmware `management_service` with explicit public DTO
  allowlists; passwords, API keys, and base URLs are never returned.
- Session authorization is delegated to the owner-minted management access registry.
  Runtime operations require the active secure-channel generation and pairing
  lifecycle state; wire payloads cannot set permission booleans.
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
- The unauthenticated physical transport surface exposes strict `system.hello` and
  the locally gated pairing RPC sequence. It rejects unknown fields, embedded NUL
  bytes, malformed IDs, and every management operation until an authenticated
  encrypted session exists. Confirmed sessions expose only the allowlisted status,
  public-configuration, and Wi-Fi methods.
- The USB owner task keeps the decoder off the task stack, clears temporary
  buffers, caps JSON depth, validates escapes before cJSON, rate-limits all
  inbound bytes, samples malformed-input logs, and never logs untrusted payloads.
- The browser Web Serial adapter can select a device, exchange and validate the
  framed hello response, enforce a timeout, and close stalled connections without
  displaying device-supplied error details.
- The isolated `management_session` component now implements the cryptographic
  core for MbedTLS ECJPAKE with fixed client/device roles, SHA-256, P-256, and
  uncompressed points. It has no dependency on the runtime, configuration store,
  or management service.
- The canonical transcript binds protocol and suite versions, both roles,
  credential and handshake IDs, device identity, USB transport identity, both
  nonces, and all four raw ECJPAKE messages. HKDF derives separate directional
  AES-256-GCM keys, nonce prefixes, Finished keys, and a session identifier.
- Both peers must verify role-specific HMAC-SHA-256 Finished proofs before a
  session can be activated. Provisional key material stays behind an opaque,
  single-use handle and cannot be consumed by the secure channel until the local
  proof is created and the peer proof is verified. The secure record layer
  enforces client-to-device requests and device-to-client responses/events,
  authenticates the outer
  `AQMG` header and inner `AQSE` header, uses direction-specific 96-bit nonces,
  requires exact monotonic counters, and does not advance receive state after an
  authentication failure.
- Host-side tests build against ESP-IDF's vendored MbedTLS and cover matching and
  mismatched codes, transcript identity mismatch, tampered ECJPAKE messages,
  random-source failure, Finished confirmation, AEAD tampering, outer-frame
  substitution, direction/session substitution, and replay rejection under
  AddressSanitizer and UndefinedBehaviorSanitizer.
- The isolated device-owned pairing lifecycle now defaults closed and requires a
  consumed local-presence event before it displays an eight-digit one-time code.
  Code generation uses rejection sampling, preserves leading zeroes, and keeps
  the code only in volatile memory and the local display path.
- Each locally opened window is bound to the current USB connection generation.
  A different connection cannot begin the handshake, and disconnecting the
  bound connection clears the displayed code and closes the window even before
  PAKE starts.
- The pairing window lasts 120 seconds, each handshake step has a 15-second
  deadline, and only one handshake may be active. An online attempt is reserved
  through the persistence port before PAKE processing begins; disconnects,
  timeouts, protocol failures, and reboots do not refund it.
- Five failed attempts produce a persistent lockout. Recovery requires a
  separate consumed local-reset event and a successful persistent record reset.
  Corrupt records and clock, entropy, display, or persistence failures all fail
  closed.
- A failed or abandoned attempt closes its pairing window, so every retry
  requires a new physical local-presence action. This hard local gate plus the
  fifth-attempt persistent lock is the selected policy instead of a remotely
  retryable cooldown/backoff sequence.
- Client Finished verification moves the lifecycle to a pending state only. A
  session becomes active after the device Finished message is fully sent and the
  attempt counter is durably reset. Active sessions enforce a five-minute idle
  timeout and a 30-minute absolute timeout through an injected monotonic clock.
- Host lifecycle tests use injected clock, entropy, display, presence,
  persistence, and connection-revocation ports. They cover exact timeout
  boundaries, persistent reboot lockout, single-handshake ownership, failure
  cleanup, and successful session activation under AddressSanitizer and
  UndefinedBehaviorSanitizer; lifecycle line and branch coverage both exceed
  80%.
- Display clearing is now an observable lifecycle operation. A failed show or
  clear synchronously revokes the bound connection and enters `FAULT`; it never
  resumes pairing or refunds the reserved attempt. A later physical reset may
  retry the clear operation before resetting persistent state.
- The production ESP-IDF NVS adapter uses the independent `aiqa_pair` namespace
  and one `u64` `lock` item encoding only `{version, attempts_used}`. Every store
  performs set, commit, and close; a successful commit is followed by an
  independent read-only reopen and exact readback.
  A commit error always fails closed even when the write may already have
  reached flash; restart then converges from the durable value. A successful
  commit is accepted only when independent readback matches exactly.
- Host power-cut tests cover unapplied commit errors, applied commit errors,
  applied-but-unreadable commits, corrupt version/attempt values, type mismatch,
  reset persistence, and reboot round-trips. Both lifecycle and NVS adapter line
  and branch coverage exceed 80%.

### Device Management Pause Checkpoint (2026-07-14)

Implementation is intentionally paused at the first usable physical-management
vertical slice. Estimated completion is approximately 40% of the full operator
experience and 75-80% of the shared security, transport, and configuration foundation.

The implemented scope pending repeatable physical acceptance is:

- Select the ESP32-S3 through Chromium Web Serial.
- Open a local pairing window with three short BOOT presses, display an eight-digit
  one-time code, and establish a confirmed encrypted ECJPAKE/AES-GCM session.
- Read `device.status.get` and `config.public.get` through the authenticated channel.
- Read and update Wi-Fi SSID/password state through `wifi.update`, including revision
  conflict handling, asynchronous operation polling, trial connection, activation,
  rollback, and recovery-required quarantine.
- Exercise the same status and Wi-Fi surfaces through `SimulatedDeviceTransport`.

The following user-visible capabilities are not implemented yet:

- Provider, chat/ASR/TTS model, TTS voice, reasoning, and token-setting writes.
- Write-only API-key replace/clear operations.
- Management UI writes for the persisted language and assistant-profile preferences.
  Voice commands already persist these preferences on the device.
- Custom Prompt storage, validation, reset-to-default behavior, and runtime use.
- Animal-image selection. The current pet is compiled into the firmware and cannot
  be changed by the client.
- Simulation/debug scenarios beyond the current status and Wi-Fi simulator.
- Pixel-accurate display mirroring. The client currently renders a logical CSS preview
  using status text; it does not stream the 466x466 framebuffer.
- End-to-end browser automation, signed desktop packaging, deployment, and update flow.

Known acceptance and delivery risks at the pause point:

- The client Wi-Fi operation timeout is 60 seconds, equal to the firmware's worst-case
  candidate-connect plus rollback window. Resume work by adding margin or a durable
  operation-query/event mechanism before declaring Wi-Fi failure.
- The connection indicator is derived from transport mode and does not yet transition
  reliably after physical disconnect or secure-session expiry.
- `contracts/device-management.schema.json` does not yet describe the full public
  configuration response and must be brought back in sync before adding write DTOs.
- Unit and host coverage is strong, but the physical pairing/status/Wi-Fi flow still
  requires a repeatable real-device acceptance suite.
- A successful dependency audit is still required before a release; an interrupted or
  malformed registry response is not evidence that dependencies are vulnerability-free.
- Production release still requires Secure Boot v2, Flash/NVS encryption, signed images,
  and a separately reviewed eFuse procedure. Development hardware must retain a
  reversible profile.

Resume in this order:

1. Close the current physical acceptance slice: disconnect/reconnect lifecycle, Wi-Fi
   timeout margin, successful update, failed-password rollback, and revision conflict
   tests (estimated 2-4 person-days).
2. Add a general atomic configuration update contract for provider/model/voice/options
   and write-only chat/ASR keys, then implement the client forms (7-12 person-days).
3. Add profile/language management UI and Prompt configuration with explicit
   append-versus-replace semantics and reset-to-default behavior (5-8 person-days).
4. Add selection among firmware-bundled pet designs (4-7 person-days). Arbitrary image
   upload is a separate feature requiring chunked transfer, validation, dual-slot Flash,
   integrity checks, rollback, and expression compatibility.
5. Expand simulation/debug, add browser and physical-device E2E coverage, update docs,
   and finish the selected browser or desktop delivery path (7-14 person-days).

For a browser client with firmware-bundled pet selection, the remaining MVP is estimated
at 25-35 person-days when phase 5 is limited to the minimum E2E and browser-delivery
slice. Completing every phase 5 item raises the estimate to 25-45 person-days. Arbitrary
image upload, framebuffer mirroring, signed desktop packaging, and production hardware
security increase the remaining scope to roughly 40-60 person-days.

Quality baseline recorded at this checkpoint:

- Host/firmware tests: 162 passed.
- Client tests: 69 passed across 11 files.
- Client coverage: 95.05% lines and 81.73% branches.
- Client production build and ESP-IDF firmware build: passed.

Hardware state recorded at this checkpoint:

- The blank-screen root cause was confirmed through JTAG as an `aiqa_state` task stack
  overflow. A 4 KiB stack failed, an 8 KiB stack still failed, and the current 12 KiB
  stack was verified with the LCD ready and all runtime tasks alive.
- The finalized original blue pet design is compiled into the C sprite renderer and
  has been flashed to the development device. It is not yet a client-selectable asset.
- The verified development device uses ESP32-S3 native USB Serial/JTAG at
  `/dev/cu.usbmodem1101`. Browser serial code must release DTR/RTS to their safe idle
  state so a management connection does not leave the device in ROM download mode.

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
  between Chinese and English, persist the preference in NVS, play a local
  confirmation, and pass `zh`/`en` response-language hints into later chat requests.
- Assistant name and gender commands persist a validated profile in NVS. The
  profile is injected as a dedicated system context, separate from recent dialogue.
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

- Repeatable real-device acceptance for live provider calls across every supported
  provider/model combination.

## Current Pet UI Scope

Implemented:

- Centered 160x160 RGB565 procedural C sprite for the original blue pet design, with
  asymmetric ears, layered blue fur, a striped tail, and a coral star-shaped tail tip.
- Circular-safe sprite layout contract for the 1.75C round AMOLED safe area.
- Multi-frame pet animation and scene mapping across all 14 supported expressions.
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
- The ASR task reserves 16 KiB of stack, keeps its PCM/Base64 request workspace
  on the heap, and securely drains queued PCM when a newer interaction cancels
  pending transcription.
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

- Full Unicode/multiline answer rendering and pagination on the circular screen;
  the current streaming path redraws a compact `OUT` dialogue view.
- Repeatable real-device soak of the complete PTT -> ASR -> streamed chat -> TTS path.

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
- ASR checks both total internal heap and the largest contiguous internal block
  before opening HTTP/TLS, returning a recoverable failure when memory is low.
- Captured ES7210 PCM is wrapped as an in-memory WAV for ASR.
- Chat streaming now parses SSE `delta.content` chunks and redraws the pet
  dialogue page while the model is still answering.

Not yet implemented:

- Runtime heap gate enforcement before chat HTTP requests.
- Runtime cooldown persistence across reboot.
- Long-running on-device soak with Wi-Fi and provider APIs.

Hardware and release security not yet implemented:

- Production PWR-button event decoding and PMU policy beyond the current AXP2101
  status reads.
- Production ES8311/PA pop-suppression tuning and long-running playback validation.
- Production secret hardening with ESP-IDF NVS encryption:
  - flash encryption plus `nvs_keys`, or
  - HMAC eFuse key derivation.
