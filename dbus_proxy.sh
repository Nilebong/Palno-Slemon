#!/bin/bash
# dbus_proxy.sh — Proxies D-Bus into the active slemon_app directory for Palno BLE
ACTIVE_APP=$(ls -td /home/arduino/ArduinoApps/slemon_app* 2>/dev/null | head -1)

if [ -z "$ACTIVE_APP" ] || [ ! -d "$ACTIVE_APP" ]; then
    ACTIVE_APP="/home/arduino/ArduinoApps/slemon_app_v5"
fi

echo "[DBUS] Active app is: $ACTIVE_APP"
rm -f "$ACTIVE_APP/system_bus_socket"
exec /usr/bin/socat UNIX-LISTEN:"$ACTIVE_APP/system_bus_socket",fork,unlink-early,mode=777 UNIX-CONNECT:/var/run/dbus/system_bus_socket
