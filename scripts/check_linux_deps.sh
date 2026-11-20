#!/usr/bin/env bash
set -euo pipefail

binary="${1:?usage: check_linux_deps.sh BINARY [MAX_GLIBC]}"
max_glibc="${2:-2.35}"

allowed='^(linux-vdso\.so\.1|ld-linux-x86-64\.so\.2|libc\.so\.6|libm\.so\.6|libdl\.so\.2|libpthread\.so\.0|librt\.so\.1)$'

status=0
echo "Shared libraries of $binary:"
while read -r name _; do
    [ -z "$name" ] && continue
    if [[ "$name" == /* ]]; then
        name="$(basename "$name")"
    fi
    if [[ "$name" =~ $allowed ]]; then
        echo "  ok       $name"
    else
        echo "  UNEXPECTED $name"
        status=1
    fi
done < <(ldd "$binary" | sed 's/^[[:space:]]*//')

highest="$(objdump -T "$binary" | grep -o 'GLIBC_[0-9][0-9.]*' | sed 's/GLIBC_//' | sort -V | tail -1)"
echo "Highest GLIBC symbol version: ${highest:-none} (allowed: $max_glibc)"
if [ -n "$highest" ] && [ "$(printf '%s\n%s\n' "$highest" "$max_glibc" | sort -V | tail -1)" != "$max_glibc" ]; then
    echo "The binary needs a newer glibc than $max_glibc"
    status=1
fi

exit $status
