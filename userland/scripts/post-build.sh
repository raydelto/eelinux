#!/bin/sh
# X is an opt-in session: a login shell remains available on the 32 MiB console.
set -eu
target=$1
test -d "$target/etc/init.d"
rm -f -- "$target/etc/init.d/S40xorg"
# Shadow installs login in /usr/bin; SysV getty searches /bin/login.
if test -x "$target/usr/bin/login" && ! test -e "$target/bin/login"; then
    ln -s ../usr/bin/login "$target/bin/login"
fi
# Shadow also installs groups. Keep the GNU Coreutils implementation selected
# consistently even when Shadow finishes after Coreutils.
test -x "$target/usr/bin/coreutils"
ln -sf ../usr/bin/coreutils "$target/bin/groups"
