#!/usr/bin/env bash
# Launch the Headroom optimization proxy, fully detached.
#
# Why a script (and not an inline postStartCommand): VS Code runs the postStart
# lifecycle command and then terminates its process group when the command
# returns. A plain `nohup ... &` proxy gets SIGTERM'd and dies; inline
# `setsid ... &` strings are also fragile because the lifecycle wrapper can
# mangle the shell redirection/background operators. Running everything inside
# this script means VS Code only sees a single `bash start-headroom.sh`
# invocation with no shell operators to misparse, and `setsid` puts the proxy
# in its own session so it survives.
set -u

PORT=8787
BIN="/home/esp/.local/bin/headroom"
LOG="/tmp/headroom.log"
MARKER="/tmp/headroom-start.log"

# Timestamped marker proves this script actually ran (diagnostic).
echo "[$(date -Is)] start-headroom.sh invoked" >> "$MARKER"

# Don't spawn a second instance if the proxy is already up.
if curl -fsS "http://127.0.0.1:${PORT}/livez" >/dev/null 2>&1; then
    echo "[$(date -Is)] proxy already healthy on ${PORT}, skipping launch" >> "$MARKER"
    exit 0
fi

if [ ! -x "$BIN" ]; then
    echo "[$(date -Is)] ERROR: headroom binary not found at ${BIN}" >> "$MARKER"
    exit 1
fi

# Detach into a new session so the proxy outlives the postStart shell.
# --memory: enable persistent memory.  --no-telemetry: disable anonymous stats.
/usr/bin/setsid "$BIN" proxy --port "$PORT" --memory --no-telemetry </dev/null >>"$LOG" 2>&1 &
echo "[$(date -Is)] launched headroom proxy (wrapper pid $!)" >> "$MARKER"

