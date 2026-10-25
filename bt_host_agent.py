#!/usr/bin/env python3
"""
bt_host_agent.py — Host-side Bluetooth Assistant Service for Uno Q (Linux)
Handles Classic Bluetooth A2DP audio pairing and connection for ZEBRONICS speaker,
and device discovery via BlueZ and HCI tools across all active app versions.
"""

import time
import json
import os
import glob
import subprocess
import logging

logging.basicConfig(level=logging.INFO, format="%(asctime)s [BT_HOST] %(message)s")

APPS_BASE = "/home/arduino/ArduinoApps"

def run_cmd(cmd_list, timeout=10):
    try:
        res = subprocess.run(cmd_list, capture_output=True, text=True, timeout=timeout)
        out = (res.stdout or "") + " " + (res.stderr or "")
        return out.strip()
    except Exception as e:
        return str(e)

def get_known_devices():
    """Return dict of {mac: name} from bluetoothctl devices."""
    devices = {}
    out = run_cmd(["bluetoothctl", "devices"], timeout=4)
    for line in out.splitlines():
        parts = line.split(maxsplit=2)
        if len(parts) >= 3 and parts[0] == "Device":
            mac = parts[1].strip().upper()
            name = parts[2].strip()
            devices[mac] = name
    return devices

def trigger_short_scan(duration=4.0):
    """Run a non-blocking bluetoothctl scan for a few seconds to populate BlueZ cache."""
    try:
        p = subprocess.Popen(["bluetoothctl", "scan", "on"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        time.sleep(duration)
        p.terminate()
        try:
            p.wait(timeout=2)
        except Exception:
            p.kill()
    except Exception as e:
        logging.warning("Scan error: %s", e)

def process_command(cmd_data):
    action = cmd_data.get("action")
    mac = cmd_data.get("mac", "").strip().upper()
    logging.info("Processing action: %s, mac: %s", action, mac)

    if action == "connect_speaker":
        if not mac:
            mac = "30:21:44:62:2C:58"
        
        # 1. Power on adapter and agent
        run_cmd(["bluetoothctl", "power", "on"], timeout=3)
        run_cmd(["bluetoothctl", "agent", "on"], timeout=2)
        run_cmd(["bluetoothctl", "default-agent"], timeout=2)

        # 2. Check if device is known to BlueZ; if not, do brief discovery
        known = get_known_devices()
        if mac not in known:
            logging.info("Device %s not known, performing short scan...", mac)
            trigger_short_scan(duration=4.0)

        # 3. Trust the device
        run_cmd(["bluetoothctl", "trust", mac], timeout=3)

        # 4. Pair if needed
        pair_out = run_cmd(["bluetoothctl", "pair", mac], timeout=8)
        logging.info("Pair output: %s", pair_out)

        # 5. Connect
        conn_out = run_cmd(["bluetoothctl", "connect", mac], timeout=8)
        logging.info("Connect output: %s", conn_out)

        # 6. Check for PulseAudio A2DP sink
        time.sleep(1.0)
        sink_name = f"bluez_sink.{mac.replace(':', '_')}.a2dp_sink"
        run_cmd(["pactl", "set-default-sink", sink_name], timeout=3)

        # Verify connection status
        info_out = run_cmd(["bluetoothctl", "info", mac], timeout=3)
        is_connected = "Connected: yes" in info_out or "Connection successful" in conn_out

        return {
            "success": is_connected,
            "mac": mac,
            "sink": sink_name if is_connected else "",
            "pair_out": pair_out,
            "conn_out": conn_out,
            "connected": is_connected
        }

    elif action == "scan":
        logging.info("Starting comprehensive Bluetooth scan...")
        trigger_short_scan(duration=4.0)
        run_cmd(["hcitool", "inq"], timeout=4)
        devices_dict = get_known_devices()

        h_out = run_cmd(["hcitool", "scan"], timeout=5)
        for line in h_out.splitlines():
            line = line.strip()
            if line and not line.startswith("Scanning"):
                parts = line.split(maxsplit=1)
                if len(parts) == 2:
                    m = parts[0].strip().upper()
                    n = parts[1].strip()
                    if m not in devices_dict:
                        devices_dict[m] = n

        if "30:21:44:62:2C:58" not in devices_dict:
            devices_dict["30:21:44:62:2C:58"] = "ZEBRONICS (Preset Speaker)"
        if "3C:8A:1F:BB:4D:16" not in devices_dict:
            devices_dict["3C:8A:1F:BB:4D:16"] = "Palno-ESP32 (Preset Wearable)"

        dev_list = [{"mac": m, "name": n} for m, n in devices_dict.items()]
        logging.info("Scan completed, found %d devices", len(dev_list))
        return {"success": True, "devices": dev_list}

    return {"success": False, "message": f"Unknown action: {action}"}

def main():
    logging.info("Bluetooth Host Bridge Agent starting across all apps...")
    while True:
        try:
            # Check all app directories for bt_cmd.json
            for app_dir in glob.glob(os.path.join(APPS_BASE, "slemon_app*")):
                cmd_file = os.path.join(app_dir, "bt_cmd.json")
                res_file = os.path.join(app_dir, "bt_res.json")
                if os.path.exists(cmd_file):
                    data = None
                    try:
                        with open(cmd_file, "r") as f:
                            data = json.load(f)
                        os.remove(cmd_file)
                    except Exception as e:
                        try: os.remove(cmd_file)
                        except Exception: pass

                    if data:
                        res = process_command(data)
                        try:
                            with open(res_file, "w") as f:
                                json.dump(res, f)
                        except Exception as e:
                            logging.warning("Error writing %s: %s", res_file, e)
        except Exception as e:
            logging.error("Loop error: %s", e)
        time.sleep(0.3)

if __name__ == "__main__":
    main()
