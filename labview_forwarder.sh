#!/bin/bash
# labview_forwarder.sh — Dynamically forwards port 5005 to whichever container is active

echo "[FORWARDER] Dynamic LabVIEW Port 5005 Forwarder starting..."

while true; do
    CONTAINER_ID=$(docker ps -q --filter "name=main-1" | head -1)
    if [ -n "$CONTAINER_ID" ]; then
        CONTAINER_IP=$(docker inspect -f '{{range .NetworkSettings.Networks}}{{.IPAddress}}{{end}}' "$CONTAINER_ID")
        if [ -n "$CONTAINER_IP" ]; then
            echo "[FORWARDER] Found container $CONTAINER_ID at $CONTAINER_IP - starting socat..."
            socat TCP-LISTEN:5005,fork,reuseaddr TCP:"$CONTAINER_IP":5005
        fi
    fi
    sleep 2
done
