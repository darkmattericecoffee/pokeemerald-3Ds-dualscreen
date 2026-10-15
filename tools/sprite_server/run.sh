#!/bin/bash
# Run the plane-sharing server and put it on the internet through a Cloudflare tunnel.
#
#   tools/sprite_server/run.sh                      # quick tunnel: a new https://*.trycloudflare.com each start
#   TUNNEL=emerald3ds tools/sprite_server/run.sh    # a named tunnel with a fixed address (README, "For real")
#
# PORT (default 8787) and DATA (default ~/emerald3ds-sprite-server) can be set too.
# Ctrl-C stops both. Needs cloudflared (brew install cloudflared).
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
PORT="${PORT:-8787}"
DATA="${DATA:-$HOME/emerald3ds-sprite-server}"
export PATH="/opt/homebrew/bin:/usr/local/bin:$PATH"

command -v cloudflared >/dev/null || { echo "cloudflared is missing: brew install cloudflared" >&2; exit 1; }

python3 "$HERE/server.py" --data "$DATA" serve --port "$PORT" &
SERVER=$!
trap 'kill "$SERVER" 2>/dev/null || true' EXIT INT TERM

for _ in $(seq 1 40); do
    curl -fs "http://127.0.0.1:$PORT/api/health" >/dev/null 2>&1 && break
    kill -0 "$SERVER" 2>/dev/null || { echo "the server did not start" >&2; exit 1; }
    sleep 0.25
done

if [ -n "${TUNNEL:-}" ]; then
    cloudflared tunnel --no-autoupdate run --url "http://127.0.0.1:$PORT" "$TUNNEL"
else
    echo "Starting a quick tunnel; the https://...trycloudflare.com address appears below." >&2
    cloudflared tunnel --no-autoupdate --url "http://127.0.0.1:$PORT"
fi
