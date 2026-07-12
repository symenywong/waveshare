# Waveshare AI Pet

ESP-IDF project for an AI electronic pet on the Waveshare
ESP32-S3-Touch-AMOLED-1.75C. The pet can answer knowledge questions, but the
primary product surface is a companion-style pet UI with expressive on-screen
moods.

The first implementation stage establishes the safe runtime skeleton:

- event-driven FreeRTOS task ownership
- board constants for the 1.75C hardware
- I2C scan and required-device detection for PMU/audio codecs
- CO5300 AMOLED QSPI bring-up with a circular-safe pet expression page
- BOOT long-press push-to-talk event source and PA safe-off default
- bounded 16 kHz/16-bit/mono audio capture budget and recording session lifecycle
- provider capability contracts for Qwen/DashScope and MiniMax
- secure configuration validation and redaction tooling
- local tests for provider/config safety rules

## Current Build Status

Verified locally with ESP-IDF v5.5.2 from `/Users/symeny/esp/esp-idf`:

```bash
. /Users/symeny/esp/esp-idf/export.sh
idf.py set-target esp32s3
idf.py build
idf.py -p /dev/cu.usbmodem1101 flash
```

On boot the current firmware prints:

- board pin constants
- safe PA-off initialization result
- I2C scan addresses and missing required devices
- BOOT button pressed/released state
- audio capture budget
- BOOT long-press PTT event transitions
- ES7210/I2S PCM capture diagnostics after long-press
- AMOLED init status and pet expression-page updates
- NVS provisioning status
- Wi-Fi/SNTP network task status after provisioning
- Qwen chat status after ASR transcript handoff
- static Qwen ASR sample transcription status after PTT release

Local host-side checks:

```bash
python3 -m unittest discover -s tests
```

The firmware currently stops at `ERROR/CONFIG_MISSING` until the `aiqa` NVS
namespace is provisioned with Wi-Fi credentials and a chat API key. The AMOLED
screen shows a readable pet page; without provisioning it should display
`AI PET`, `SETUP NEEDED`, `NVS CONFIG MISSING`, and `RUN PROVISION TOOL`.
Long-pressing BOOT now drives the runtime into the `LISTENING` pet state and
release moves it toward transcription. The ES7210/I2S capture path now starts on
press, stops on release, and logs PCM byte/sample/peak statistics for microphone
bring-up. The ASR worker still uses the static Qwen sample until recorded PCM is
encoded and passed into the ASR request.
For Phase 6, transcription uses a static public audio URL through Qwen ASR via
the OpenAI-compatible `chat/completions` endpoint; this proves provider
selection, TLS/HTTP behavior, and runtime ASR events before microphone PCM is
available.
For Phase 7, the ASR transcript is handed to the configured Qwen chat model and
the result maps back into the pet state machine.
Phase 9 adds release-hardening contracts for repeated PTT cycles, minimum heap
before model requests, provider rate-limit cooldown, and privacy defaults that
keep transcripts and answers out of logs.

## Configuration

Do not pass API keys on the command line. The provisioning helper reads keys
from stdin or environment variables and redacts output:

```bash
printf '%s\n' 'sk-...' | python3 tools/provision_config.py \
  --provider dashscope_openai_chat \
  --base-url https://dashscope.aliyuncs.com/compatible-mode/v1 \
  --model qwen3.7-max \
  --asr-provider dashscope_qwen_asr_flash \
  --asr-base-url https://dashscope.aliyuncs.com/compatible-mode/v1 \
  --asr-model qwen3-asr-flash \
  --dry-run
```

The default chat model ID is `qwen3.7-max`. Keep it as a provider model ID,
not a display string such as `qwen 3.7 max`.
MiniMax can be selected as the chat provider with `--provider minimax_openai_chat`,
`--base-url https://api.minimax.io/v1`, and `--model MiniMax-M3`; ASR stays on
the independent `--asr-*` settings unless you override them.

To create an ESP-IDF NVS CSV for the device:

```bash
printf '%s\n' "$AIQA_API_KEY" | python3 tools/provision_config.py \
  --provider dashscope_openai_chat \
  --base-url https://dashscope.aliyuncs.com/compatible-mode/v1 \
  --model qwen3.7-max \
  --asr-provider dashscope_qwen_asr_flash \
  --asr-base-url https://dashscope.aliyuncs.com/compatible-mode/v1 \
  --asr-model qwen3-asr-flash \
  --wifi-ssid "$WIFI_SSID" \
  --wifi-password "$WIFI_PASSWORD" \
  --nvs-csv aiqa.secrets.csv \
  --allow-plaintext-nvs
```

Then generate and flash the NVS partition:

```bash
. /Users/symeny/esp/esp-idf/export.sh
python "$IDF_PATH/components/nvs_flash/nvs_partition_generator/nvs_partition_gen.py" \
  generate aiqa.secrets.csv aiqa.nvs.bin 0x6000
python -m esptool --chip esp32s3 -p /dev/cu.usbmodem1101 write_flash 0x9000 aiqa.nvs.bin
```

This plaintext NVS path is for bring-up only. Production firmware must enable
ESP-IDF NVS encryption using either flash encryption with an `nvs_keys`
partition or the HMAC eFuse scheme before storing Wi-Fi passwords or API keys
on the device.

`*.secrets.csv` and `*.nvs.bin` are ignored by git. Treat them as sensitive.
