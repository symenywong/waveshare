#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir="${repo_root}/build-release"
signed_dir="${build_dir}/signed"
signing_key="${AIQA_SECURE_BOOT_SIGNING_KEY:?Set AIQA_SECURE_BOOT_SIGNING_KEY to an externally managed RSA-3072 private key}"

if [[ ! -f "${signing_key}" ]]; then
  echo "Secure Boot signing key does not exist: ${signing_key}" >&2
  exit 1
fi
if [[ ! -f "${build_dir}/bootloader/bootloader.bin" || ! -f "${build_dir}/waveshare_ai_pet.bin" ]]; then
  echo "Run scripts/build-release-security.sh before signing" >&2
  exit 1
fi

mkdir -p "${signed_dir}"
idf.py secure-sign-data \
  --keyfile "${signing_key}" \
  --output "${signed_dir}/bootloader.bin" \
  "${build_dir}/bootloader/bootloader.bin"
idf.py secure-sign-data \
  --keyfile "${signing_key}" \
  --output "${signed_dir}/waveshare_ai_pet.bin" \
  "${build_dir}/waveshare_ai_pet.bin"
espsecure.py signature_info_v2 "${signed_dir}/bootloader.bin"
espsecure.py signature_info_v2 "${signed_dir}/waveshare_ai_pet.bin"
