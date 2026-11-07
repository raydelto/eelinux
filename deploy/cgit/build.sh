#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
# Build on the hosting server, or a Linux environment matching its architecture/libc.
set -eu

usage() {
    printf '%s\n' 'Usage: sh deploy/cgit/build.sh /absolute/build-root [jobs] [dynamic|static]' >&2
    exit 2
}

[ "$#" -ge 1 ] && [ "$#" -le 3 ] || usage
build_root=$1
jobs=${2:-2}
mode=${3:-dynamic}
case "$mode" in dynamic|static) ;; *) usage ;; esac
case "$build_root" in
    /*) ;;
    *) printf '%s\n' 'The build root must be an absolute Linux path.' >&2; exit 2 ;;
esac
case "$build_root" in
    /|*[!a-zA-Z0-9_./+-]*) printf '%s\n' 'Use a non-root build path without spaces or shell metacharacters.' >&2; exit 2 ;;
esac
case "$jobs" in
    ''|*[!0-9]*) usage ;;
esac
[ "$jobs" -ge 1 ] && [ "$jobs" -le 64 ] || usage

for program in git make cc python3; do
    command -v "$program" >/dev/null 2>&1 || { printf 'Required program missing: %s\n' "$program" >&2; exit 1; }
done

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
lock=$script_dir/source-lock.json
commit=$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["commit"])' "$lock")
repository=$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["repository"])' "$lock")
tag=$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["release"])' "$lock")
git_repository=$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["git_submodule_repository"])' "$lock")
case "$commit" in
    *[!0-9a-f]*|'') printf '%s\n' 'Invalid cgit source-lock commit.' >&2; exit 1 ;;
esac
[ "${#commit}" -eq 40 ] || exit 1

mkdir -p -- "$build_root"
build_root=$(CDPATH= cd -- "$build_root" && pwd -P)
project_root=$(CDPATH= cd -- "$script_dir/../.." && pwd -P)
case "$build_root/" in
    "$project_root/"*) printf '%s\n' 'Keep cgit build output outside the source checkout.' >&2; exit 2 ;;
esac
source_dir=$build_root/cgit-$commit
if [ "$mode" = static ]; then source_dir=$source_dir-static; fi
if [ -e "$source_dir" ]; then
    [ -d "$source_dir/.git" ] && [ -f "$source_dir/.eelinux-cgit-build" ] || {
        printf 'Refusing an existing unmanaged build directory: %s\n' "$source_dir" >&2; exit 1;
    }
    [ "$(cat "$source_dir/.eelinux-cgit-build")" = "$commit" ] || exit 1
    [ "$(git -C "$source_dir" remote get-url origin)" = "$repository" ] || exit 1
    [ "$(git -C "$source_dir" rev-parse HEAD)" = "$commit" ] || {
        printf '%s\n' 'Existing cgit checkout has a different commit; use a fresh build root.' >&2; exit 1;
    }
    git -C "$source_dir" diff --quiet --ignore-submodules=untracked
    git -C "$source_dir" diff --cached --quiet --ignore-submodules=untracked
else
    git clone --no-checkout --single-branch --branch "$tag" --depth 1 "$repository" "$source_dir"
    actual=$(git -C "$source_dir" rev-parse "${tag}^{commit}")
    [ "$actual" = "$commit" ] || {
        printf '%s\n' 'Official release tag does not match the locked commit; build stopped.' >&2; exit 1;
    }
    git -C "$source_dir" checkout --detach "$commit"
    printf '%s\n' "$commit" > "$source_dir/.eelinux-cgit-build"
fi

git -C "$source_dir" -c protocol.file.allow=never -c submodule.git.url="$git_repository" \
    submodule update --init --depth 1 -- git
expected_git=$(git -C "$source_dir" ls-tree HEAD git | awk '{print $3}')
locked_git=$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["git_submodule_commit"])' "$lock")
actual_git=$(git -C "$source_dir/git" rev-parse HEAD)
[ "$actual_git" = "$expected_git" ] && [ "$expected_git" = "$locked_git" ] || {
    printf '%s\n' 'Git submodule differs from the pinned cgit gitlink.' >&2; exit 1;
}
git -C "$source_dir/git" diff --quiet
git -C "$source_dir/git" diff --cached --quiet

# Lua and formatter filters are not needed for the initial read-only source browser.
if [ "$mode" = static ]; then
    make -C "$source_dir" -j "$jobs" NO_LUA=1 NO_OPENSSL=1 NO_CURL=1 \
        NO_EXPAT=1 NO_ICONV=1 LDFLAGS=-static
else
    make -C "$source_dir" -j "$jobs" NO_LUA=1
fi
python3 - "$source_dir" "$commit" "$actual_git" "$mode" <<'PY'
from hashlib import sha256
import json
from pathlib import Path
import sys
import struct

source = Path(sys.argv[1])
binary = source / 'cgit'
payload = binary.read_bytes()
if payload[:4] != b'\x7fELF':
    raise SystemExit('The built cgit is not a Linux ELF executable.')
order = '<' if payload[5] == 1 else '>'
if payload[4] == 2:
    phoff = struct.unpack_from(order + 'Q', payload, 32)[0]
    phsize, phnum = struct.unpack_from(order + 'HH', payload, 54)
else:
    phoff = struct.unpack_from(order + 'I', payload, 28)[0]
    phsize, phnum = struct.unpack_from(order + 'HH', payload, 42)
has_interpreter = any(struct.unpack_from(order + 'I', payload, phoff + i * phsize)[0] == 3 for i in range(phnum))
if sys.argv[4] == 'static' and has_interpreter:
    raise SystemExit('Static build requested but ELF still has a dynamic interpreter.')
report = {
    'cgit_commit': sys.argv[2],
    'git_commit': sys.argv[3],
    'binary_sha256': sha256(binary.read_bytes()).hexdigest(),
    'no_lua': True,
    'build_mode': sys.argv[4],
    'elf_has_dynamic_interpreter': has_interpreter,
}
(source / 'eelinux-build.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
print('Verified build:', source)
PY
