#!/bin/bash
# Downloads the compiler that turns the C++ core into WebAssembly, into .tools/.
# Only needed for `make wasm` and `make web`. Nothing is installed system-wide.
set -euo pipefail
cd "$(dirname "$0")/.."
VERSION=34
mkdir -p .tools/wasi-sdk
curl -fL -C - --retry 5 -o .tools/wasi-sdk.tar.gz \
  "https://github.com/WebAssembly/wasi-sdk/releases/download/wasi-sdk-${VERSION}/wasi-sdk-${VERSION}.0-arm64-macos.tar.gz"
tar -xzf .tools/wasi-sdk.tar.gz -C .tools/wasi-sdk --strip-components=1
rm .tools/wasi-sdk.tar.gz
.tools/wasi-sdk/bin/clang++ --version | head -1
