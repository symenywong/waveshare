#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"

verify() {
  local expected="$1"
  local path="$2"
  local actual
  actual="$(shasum -a 256 "$ROOT/$path" | awk '{print $1}')"
  if [[ "$actual" != "$expected" ]]; then
    echo "Pairing WASM provenance mismatch: $path" >&2
    echo "Run client/scripts/build-pairing-wasm.sh with the pinned toolchain." >&2
    exit 1
  fi
}

verify e398e40fbadef08ccfdae72f0390ac765a4b154d0c21858938f025aab4534147 client/wasm/aiqa_pairing_wasm.c
verify 265650e5843ad2867c5335e36593e5974100f2b881774e429e8f2d03f19f7c64 components/management_session/src/aiqa_pairing_client_session.c
verify bcbe3844577147de183b29d22f40dd67a2a4177b7955155253da3aabcb3f8302 components/management_session/src/aiqa_pairing_crypto.c
verify 475b8d105925a66e69ad2bddc4fb0b8cb84de6e3c388d86c5996c3e759cb2492 components/management_session/src/aiqa_secure_channel.c
verify 31d20292a4b7df7cbb71f65fc040d3ff2b76dd5db274915cb89be7ba9c4ff27e client/src/wasm/aiqaPairing.js
verify 2f0b47e9fc9cb38f129a531e1499f682ba3416a26c6a50dd98ae5cebb0adac94 client/src/wasm/aiqaPairing.wasm
