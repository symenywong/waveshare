# Waveshare AI QA

ESP-IDF project for a push-to-talk AI question-answering tool on the
Waveshare ESP32-S3-Touch-AMOLED-1.75C.

The first implementation stage establishes the safe runtime skeleton:

- event-driven FreeRTOS task ownership
- board constants for the 1.75C hardware
- I2C scan and required-device detection for PMU/audio codecs
- BOOT input readout and PA safe-off default
- bounded 16 kHz/16-bit/mono audio capture budget
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
- NVS provisioning status
- Wi-Fi/SNTP network task status after provisioning

Local host-side checks:

```bash
python3 -m unittest discover -s tests
```

The firmware currently stops at `ERROR/CONFIG_MISSING` until the `aiqa` NVS
namespace is provisioned with Wi-Fi credentials and a chat API key. The AMOLED
panel driver is not implemented yet, so a black screen does not mean the
firmware failed to boot; use the USB serial monitor for bring-up status.

## Configuration

Do not pass API keys on the command line. The provisioning helper reads keys
from stdin or environment variables and redacts output:

```bash
printf '%s\n' 'sk-...' | python3 tools/provision_config.py \
  --provider dashscope_openai_chat \
  --base-url https://dashscope.aliyuncs.com/compatible-mode/v1 \
  --model qwen3.7-max \
  --dry-run
```

The default chat model ID is `qwen3.7-max`. Keep it as a provider model ID,
not a display string such as `qwen 3.7 max`.

To create an ESP-IDF NVS CSV for the device:

```bash
printf '%s\n' "$AIQA_API_KEY" | python3 tools/provision_config.py \
  --provider dashscope_openai_chat \
  --base-url https://dashscope.aliyuncs.com/compatible-mode/v1 \
  --model qwen3.7-max \
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
