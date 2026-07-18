#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
EMSDK_DIR="${EMSDK_DIR:-/tmp/aiqa-emsdk}"
IDF_PATH="${IDF_PATH:-$HOME/esp/esp-idf}"
MBEDTLS_SOURCE="${MBEDTLS_SOURCE:-$IDF_PATH/components/mbedtls/mbedtls}"
BUILD_DIR="${AIQA_WASM_BUILD_DIR:-$ROOT/client/.wasm-build}"
OUTPUT_DIR="$ROOT/client/src/wasm"
EXPECTED_EMSDK_COMMIT="c0bb220cb6e6f4e0fabb6f6db9efd53390ef5e56"
EXPECTED_IDF_COMMIT="30aaf64524299d3bde422ca9a2848090d1bc5d0f"

if [[ ! -f "$EMSDK_DIR/emsdk_env.sh" ]]; then
  echo "emsdk 4.0.23 is required at $EMSDK_DIR" >&2
  exit 1
fi
if [[ ! -f "$MBEDTLS_SOURCE/library/ecjpake.c" ]]; then
  echo "Mbed TLS source is unavailable at $MBEDTLS_SOURCE" >&2
  exit 1
fi
if [[ "$(git -C "$EMSDK_DIR" rev-parse HEAD 2>/dev/null)" != "$EXPECTED_EMSDK_COMMIT" ]]; then
  echo "emsdk must be pinned to $EXPECTED_EMSDK_COMMIT (4.0.23)" >&2
  exit 1
fi
if [[ "$(git -C "$IDF_PATH" rev-parse HEAD 2>/dev/null)" != "$EXPECTED_IDF_COMMIT" ]]; then
  echo "ESP-IDF must be pinned to $EXPECTED_IDF_COMMIT (v5.5.2)" >&2
  exit 1
fi
if [[ -n "$(git -C "$EMSDK_DIR" status --porcelain)" ||
      -n "$(git -C "$IDF_PATH" status --porcelain)" ]]; then
  echo "Pinned emsdk and ESP-IDF worktrees must be clean" >&2
  exit 1
fi
PINNED_MBEDTLS_SOURCE="$(cd "$IDF_PATH/components/mbedtls/mbedtls" && pwd -P)"
ACTUAL_MBEDTLS_SOURCE="$(cd "$MBEDTLS_SOURCE" && pwd -P)"
if [[ "$ACTUAL_MBEDTLS_SOURCE" != "$PINNED_MBEDTLS_SOURCE" ]]; then
  echo "MBEDTLS_SOURCE must use the pinned ESP-IDF checkout" >&2
  exit 1
fi

source "$EMSDK_DIR/emsdk_env.sh" >/dev/null
mkdir -p "$BUILD_DIR" "$OUTPUT_DIR"

emcmake cmake \
  -S "$MBEDTLS_SOURCE" \
  -B "$BUILD_DIR/mbedtls" \
  -DENABLE_PROGRAMS=OFF \
  -DENABLE_TESTING=OFF \
  -DUSE_SHARED_MBEDTLS_LIBRARY=OFF
cmake --build "$BUILD_DIR/mbedtls" --target mbedcrypto -j 4

EXPORTED='["_aiqa_wasm_api_version","_aiqa_wasm_session_create","_aiqa_wasm_write_round_one","_aiqa_wasm_read_round_one","_aiqa_wasm_write_round_two","_aiqa_wasm_read_round_two","_aiqa_wasm_create_finished","_aiqa_wasm_verify_finished","_aiqa_wasm_encrypt_request","_aiqa_wasm_decrypt_response","_aiqa_wasm_session_destroy","_malloc","_free"]'

emcc -std=c11 -O3 -flto \
  -I"$ROOT/components/management_session/include" \
  -I"$MBEDTLS_SOURCE/include" \
  "$ROOT/client/wasm/aiqa_pairing_wasm.c" \
  "$ROOT/components/management_session/src/aiqa_pairing_client_session.c" \
  "$ROOT/components/management_session/src/aiqa_pairing_crypto.c" \
  "$ROOT/components/management_session/src/aiqa_secure_channel.c" \
  "$BUILD_DIR/mbedtls/library/libmbedcrypto.a" \
  --no-entry \
  -sALLOW_MEMORY_GROWTH=1 \
  -sENVIRONMENT=web,worker,node \
  -sEXPORTED_FUNCTIONS="$EXPORTED" \
  -sEXPORTED_RUNTIME_METHODS='["HEAPU8"]' \
  -sFILESYSTEM=0 \
  -sMODULARIZE=1 \
  -sEXPORT_ES6=1 \
  -sEXPORT_NAME=createAiqaPairingModule \
  -sSTRICT=1 \
  -o "$OUTPUT_DIR/aiqaPairing.js"

"$ROOT/client/scripts/verify-pairing-wasm.sh"
echo "Built $OUTPUT_DIR/aiqaPairing.js and aiqaPairing.wasm"
