#!/usr/bin/env bash
set -euo pipefail

# Like shared_elf_test, run this test natively on the selected target platform.
resolve_runfile() {
  local path="$1"
  if [[ ! -e "$path" && -n "${RUNFILES_DIR:-}" ]]; then
    if [[ -e "${RUNFILES_DIR}/${path}" ]]; then
      path="${RUNFILES_DIR}/${path}"
    elif [[ -e "${RUNFILES_DIR}/_main/${path}" ]]; then
      path="${RUNFILES_DIR}/_main/${path}"
    fi
  fi
  [[ -e "$path" ]] || { echo "Missing runfile: $path" >&2; return 1; }
  printf '%s/%s\n' "$(cd "$(dirname "$path")" && pwd -P)" "$(basename "$path")"
}

default_binary="$(resolve_runfile "${DEFAULT_BINARY:?}")"
dynamic_binary="$(resolve_runfile "${DYNAMIC_BINARY:?}")"
exception_library="$(resolve_runfile "${EXCEPTION_LIBRARY:?}")"
readelf="$(resolve_runfile "${READELF:?}")"
export LIBGCC_S="$(resolve_runfile "${LIBGCC_S:?}")"
export LIBSTDCXX="$(resolve_runfile "${LIBSTDCXX:?}")"
# Do not let a host libstdc++ or libgcc_s hide a broken declared runtime.
export LD_LIBRARY_PATH="$(dirname "$LIBSTDCXX"):$(dirname "$LIBGCC_S"):$(dirname "$exception_library")"

"$readelf" -d "$LIBGCC_S" > "$TEST_TMPDIR/gcc-dynamic"
grep -F 'Library soname: [libgcc_s.so.1]' "$TEST_TMPDIR/gcc-dynamic"
if grep -F 'Shared library: [libunwind.so' "$TEST_TMPDIR/gcc-dynamic"; then
  echo 'GNU ABI adapter must contain the unwinder, not rename libunwind.so' >&2
  exit 1
fi
"$readelf" --version-info "$LIBGCC_S" > "$TEST_TMPDIR/gcc-versions"
for version in GCC_3.0 GCC_3.3 GCC_3.4 GCC_4.0.0 GCC_4.2.0 GCC_4.5.0; do
  grep -F "Name: $version" "$TEST_TMPDIR/gcc-versions"
done
"$readelf" --dyn-syms --wide "$LIBGCC_S" > "$TEST_TMPDIR/gcc-symbols"
grep -F '_Unwind_RaiseException@@GCC_3.0' "$TEST_TMPDIR/gcc-symbols"
grep -F '_Unwind_Resume@@GCC_3.0' "$TEST_TMPDIR/gcc-symbols"
grep -F '_Unwind_GetIPInfo@@GCC_4.2.0' "$TEST_TMPDIR/gcc-symbols"
grep -F '__popcountdi2@@GCC_3.4' "$TEST_TMPDIR/gcc-symbols"

for binary in "$default_binary" "$dynamic_binary"; do
  "$readelf" -d "$binary" > "$TEST_TMPDIR/binary-dynamic"
  grep -F 'Shared library: [libstdc++.so.6]' "$TEST_TMPDIR/binary-dynamic"
  grep -F 'Shared library: [libgcc_s.so.1]' "$TEST_TMPDIR/binary-dynamic"
  grep -F 'Shared library: [libgnu_exception.so]' "$TEST_TMPDIR/binary-dynamic"
  if [[ "$binary" == "$default_binary" ]]; then
    if grep 'Shared library:.*gnu_project' "$TEST_TMPDIR/binary-dynamic"; then
      echo 'Default linkstatic must retain static project libraries' >&2
      exit 1
    fi
  fi
  # linkstatic=False permits dynamic project libraries; it does not require
  # cc_library to provide a DSO. Check the explicit shared boundary above.
  "$readelf" --version-info "$binary" > "$TEST_TMPDIR/binary-versions"
  grep -F 'Name: GCC_3.0' "$TEST_TMPDIR/binary-versions"
  "$binary" > "$TEST_TMPDIR/output"
  grep -Fx 'GNU runtime exception boundary passed' "$TEST_TMPDIR/output"
done
