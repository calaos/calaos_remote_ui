#!/usr/bin/env bash
# The firmware package must ask for the rescan only after every package of the
# same dpkg run is configured: an upgrade that also ships calaos-server
# restarts it there, and a rescan sent before makes the old server offer an
# image it will not be there to serve.
# Runs dpkg on a private database and root, without privileges.
set -euo pipefail

SRC="$(cd "$(dirname "$0")/.." && pwd)"
for tool in dpkg dpkg-deb; do
    command -v "$tool" >/dev/null || { echo "SKIP: no $tool"; exit 77; }
done

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
cd "$WORK"
mkdir -p admin/updates admin/info root bin
: > admin/status
: > admin/available
LOG="$WORK/order.log"

printf '#!/bin/sh\necho "rescan" >> "%s"\n' "$LOG" > bin/curl
chmod 755 bin/curl

printf 'fw' > fw.bin
bash "$SRC/scripts/build-deb.sh" testboard 9.9.9 fw.bin > /dev/null

mkdir -p srv/DEBIAN
cat > srv/DEBIAN/control <<CONTROL
Package: calaos-server-standin
Version: 1.0
Architecture: all
Maintainer: Calaos Team <team@calaos.fr>
Description: stands for calaos-server, restarted by its postinst
CONTROL
printf '#!/bin/sh\necho "server restarted" >> "%s"\n' "$LOG" > srv/DEBIAN/postinst
chmod 755 srv/DEBIAN/postinst
dpkg-deb --root-owner-group --build srv srv.deb > /dev/null

# The firmware depends on curl: an unmet dependency would make dpkg configure
# it last and hide the ordering under test.
mkdir -p curl/DEBIAN
cat > curl/DEBIAN/control <<CONTROL
Package: curl
Version: 99
Architecture: all
Maintainer: Calaos Team <team@calaos.fr>
Description: stands for curl in the private database
CONTROL
dpkg-deb --root-owner-group --build curl curl.deb > /dev/null

dpkg_run() {
    PATH="$WORK/bin:$PATH" dpkg --force-not-root --force-script-chrootless \
        --force-bad-path --log=/dev/null \
        --admindir="$WORK/admin" --instdir="$WORK/root" "$@" > dpkg.log 2>&1 || { cat dpkg.log; exit 1; }
}

fail=0
expect() {
    local what="$1" want="$2" got
    got="$(tr '\n' ' ' < "$LOG")"
    if [ "$got" != "$want" ]; then
        echo "FAIL: $what: got '$got', want '$want'"
        fail=1
    else
        echo "ok: $what"
    fi
    : > "$LOG"
}

: > "$LOG"
dpkg_run -i curl.deb
dpkg_run -i pkg/calaos-remote-ui-testboard_9.9.9_all.deb srv.deb
expect "firmware and server in one run" "server restarted rescan "

dpkg_run -i pkg/calaos-remote-ui-testboard_9.9.9_all.deb
expect "firmware alone" "rescan "

dpkg_run -r calaos-remote-ui-testboard
expect "removal" "rescan "

exit $fail
