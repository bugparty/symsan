#!/bin/bash
set -e

# 如果提供了 CLOUDFLARE_TUNNEL_TOKEN，启动 cloudflared tunnel
if [ -n "$CLOUDFLARE_TUNNEL_TOKEN" ]; then
    echo "Starting cloudflared tunnel..."
    cloudflared tunnel --no-autoupdate run --token "$CLOUDFLARE_TUNNEL_TOKEN" &
    sleep 2
    echo "Cloudflared tunnel started"
fi

# 启动 uvicorn
echo "Starting uvicorn..."
exec uvicorn app:app --host 0.0.0.0 --port 8000
