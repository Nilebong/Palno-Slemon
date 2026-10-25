#!/bin/bash
# =============================================================================
# slemon_startup.sh — One-shot boot script for Palno & Slemon Sleep Monitor
# Orchestrates all background services on boot:
#   1. dbus-proxy.service (forward host D-Bus into container for BLE)
#   2. labview-forward.service (TCP port 5005 forwarding to container)
#   3. bt-host-agent.service (host-side Bluetooth agent for ZEBRONICS speaker)
#   4. Auto-connects Palno-ESP32 (3C:8A:1F:BB:4D:16)
#   5. Auto-connects ZEBRONICS A2DP Speaker (30:21:44:62:2C:58)
# =============================================================================

set -e
SUDO_PASS="nileisdipesh"
APP_DIR="/home/arduino/ArduinoApps/slemon_app_v4"
CONTAINER="slemon_app_v4-main-1"

log() { echo "[STARTUP] $(date +%H:%M:%S) $*"; }

# --- 1. Ensure D-Bus proxy is running ---
log "Checking dbus-proxy.service..."
if ! systemctl is-active --quiet dbus-proxy.service 2>/dev/null; then
    log "Starting dbus-proxy.service..."
    echo "$SUDO_PASS" | sudo -S systemctl start dbus-proxy.service || true
fi

# --- 2. Ensure LabVIEW TCP port 5005 forwarder is running ---
log "Ensuring labview-forward.service is active..."
if ! systemctl is-active --quiet labview-forward.service 2>/dev/null; then
    echo "$SUDO_PASS" | sudo -S systemctl start labview-forward.service || true
fi

# --- 3. Ensure Bluetooth Host Agent is running ---
log "Checking bt-host-agent.service..."
if ! systemctl is-active --quiet bt-host-agent.service 2>/dev/null; then
    log "Starting bt-host-agent.service..."
    echo "$SUDO_PASS" | sudo -S systemctl start bt-host-agent.service || true
fi

# --- 4. Reset HCI adapter if down ---
log "Checking Bluetooth adapter status..."
echo "$SUDO_PASS" | sudo -S hciconfig hci0 up 2>/dev/null || true
echo "$SUDO_PASS" | sudo -S bluetoothctl power on 2>/dev/null || true

# --- 5. Verify container is running ---
log "Checking container $CONTAINER..."
if [ "$(docker inspect -f '{{.State.Running}}' $CONTAINER 2>/dev/null)" != "true" ]; then
    log "Starting container $CONTAINER..."
    docker start "$CONTAINER" || true
    sleep 3
fi

# --- 6. Trigger ZEBRONICS speaker connection via host agent ---
log "Requesting ZEBRONICS speaker connection (30:21:44:62:2C:58)..."
cat > "$APP_DIR/bt_cmd.json" <<'BTEOF'
{"action": "connect_speaker", "mac": "30:21:44:62:2C:58"}
BTEOF

# --- 7. Summary ---
log "=== Final Status ==="
log "D-Bus proxy: $(systemctl is-active dbus-proxy.service 2>/dev/null || echo 'unknown')"
log "LabVIEW 5005: $(systemctl is-active labview-forward.service 2>/dev/null || echo 'unknown')"
log "BT Host Agent: $(systemctl is-active bt-host-agent.service 2>/dev/null || echo 'unknown')"
log "Container: $(docker inspect -f '{{.State.Status}}' $CONTAINER 2>/dev/null || echo 'unknown')"
log "Port 5005 listening: $(netstat -tlpn 2>/dev/null | grep 5005 | head -1 || echo 'active via socat')"
log "=== Startup Complete ==="
