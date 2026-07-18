#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir="${repo_root}/build-release"

cd "${repo_root}"
rm -f "${build_dir}/sdkconfig" "${build_dir}/sdkconfig.old"
exec idf.py \
  -B "${build_dir}" \
  -D "SDKCONFIG=${build_dir}/sdkconfig" \
  -D "SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.release.defaults" \
  build
