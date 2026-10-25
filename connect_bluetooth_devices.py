#!/usr/bin/env python3
"""
connect_bluetooth_devices.py — Palno BLE & Bluetooth A2DP Speaker Assistant
Palno & Slemon Sleep Tracking System

Functions:
  1. Scans for Palno ESP32 BLE peripheral and Bluetooth A2DP audio speakers.
  2. Pairs, trusts, and connects Bluetooth A2DP speakers via BlueZ / bluetoothctl.
  3. Configures PulseAudio / PipeWire default audio sink for A2DP streaming.
  4. Tests binaural audio playback through the connected speaker.
  5. Saves verified MAC addresses into data/bluetooth_config.json for automatic startup reconnect.

Usage:
  python connect_bluetooth_devices.py --scan
  python connect_bluetooth_devices.py --speaker <MAC_ADDRESS>
  python connect_bluetooth_devices.py --palno <MAC_ADDRESS>
  python connect_bluetooth_devices.py --test-audio
  python connect_bluetooth_devices.py --status
"""

import sys
import os
import time
import subprocess
import argparse
import asyncio

PROJECT_ROOT = os.path.dirname(os.path.abspath(__file__))
if PROJECT_ROOT not in sys.path:
    sys.path.insert(0, PROJECT_ROOT)

from engine.config import BLE_DEVICE_NAME, BLE_SERVICE_UUID, AUDIO_DIR
from engine.bluetooth_manager import (
    BluetoothManager, load_bluetooth_config, save_bluetooth_config
)

def run_cmd(cmd_list, timeout=10.0):
    """Run system command and return stripped stdout."""
    try:
        res = subprocess.run(cmd_list, capture_output=True, text=True, timeout=timeout)
        return (res.stdout or "").strip()
    except Exception as e:
        return f"Error: {e}"

async def scan_ble_palno():
    print("\n[1/2] Scanning for Palno ESP32 Wearable (BLE)...")
    try:
        from bleak import BleakScanner
        devices = await BleakScanner.discover(timeout=6.0)
        found = []
        for d in devices:
            name = d.name or ""
            uuids = [str(u).lower() for u in d.metadata.get("uuids", [])]
            if "palno" in name.lower() or BLE_SERVICE_UUID.lower() in uuids:
                found.append(d)
                print(f"  [FOUND] Palno ESP32: Name='{d.name}' | MAC={d.address} | RSSI={d.rssi} dBm")
                save_bluetooth_config(palno_mac=d.address)
                print(f"          -> Saved {d.address} to data/bluetooth_config.json")
        if not found:
            print(f"  [!] Palno ESP32 not discovered automatically.")
            print(f"      Make sure ESP32 is powered on and flashing LED.")
            print(f"      (Advertises as '{BLE_DEVICE_NAME}' with Service UUID: {BLE_SERVICE_UUID})")
            print(f"      TIP: Check Serial Monitor for: '[BLE] Device MAC Address: XX:XX:XX:XX:XX:XX'")
    except ImportError:
        print("  [!] 'bleak' library not installed. Run: pip install bleak")
    except Exception as e:
        print(f"  [!] BLE scan error: {e}")

def scan_bluetooth_speakers():
    print("\n[2/2] Scanning for Bluetooth A2DP Speakers (Classic BT)...")
    if os.name == "nt":
        print("  [INFO] On Windows, pair your Bluetooth A2DP speaker via:")
        print("         Windows Settings -> Bluetooth & devices -> Add device.")
        return

    # Check bluetoothctl
    print("  Powering on Bluetooth adapter...")
    run_cmd(["bluetoothctl", "power", "on"], timeout=3.0)
    run_cmd(["bluetoothctl", "agent", "on"], timeout=2.0)
    run_cmd(["bluetoothctl", "default-agent"], timeout=2.0)

    print("  Scanning for nearby Bluetooth devices (6 seconds)...")
    try:
        proc = subprocess.Popen(["bluetoothctl", "scan", "on"],
                                stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        time.sleep(6.0)
        proc.terminate()
    except Exception:
        pass

    out = run_cmd(["bluetoothctl", "devices"])
    print("\nNearby Bluetooth Devices Discovered:")
    found_any = False
    if out:
        for line in out.splitlines():
            parts = line.split(maxsplit=2)
            if len(parts) >= 3 and parts[0] == "Device":
                mac = parts[1]
                name = parts[2]
                is_audio = any(w in name.lower() for w in ["speaker", "sound", "jbl", "anker", "boat", "sony", "audio", "headphone", "buds"])
                tag = "[SPEAKER / AUDIO]" if is_audio else "[BT DEVICE]"
                print(f"  {tag} MAC: {mac}  | Name: {name}")
                found_any = True
    if not found_any:
        print("  No devices found. Put your speaker into PAIRING MODE and re-run:")
        print("  python connect_bluetooth_devices.py --scan")

def connect_speaker(mac_addr):
    mac_addr = mac_addr.strip().upper()
    print(f"\n[SPEAKER] Connecting to Bluetooth A2DP Speaker: {mac_addr}...")
    bm = BluetoothManager()
    res = bm.connect_a2dp_speaker(mac_addr)
    print(f"  Result: {res.get('message', '')}")
    if res.get("sink"):
        print(f"  [OK] PulseAudio A2DP Sink: {res.get('sink')}")
        print("  Testing sound output now...")
        test_audio()

def set_palno(mac_addr):
    mac_addr = mac_addr.strip().upper()
    print(f"\n[PALNO] Setting Palno ESP32 MAC address: {mac_addr}...")
    save_bluetooth_config(palno_mac=mac_addr)
    print(f"  [OK] Saved Palno MAC {mac_addr} to data/bluetooth_config.json")
    print("  Slemon BLE client will now connect directly to this MAC address.")

def test_audio():
    print("\n[AUDIO] Playing test chime through active audio output...")
    test_wav = os.path.join(AUDIO_DIR, "wake_beta_14hz.wav")
    if not os.path.exists(test_wav):
        # Fallback to wake_beta.wav if 14hz not found
        alt_wav = os.path.join(AUDIO_DIR, "wake_beta.wav")
        if os.path.exists(alt_wav):
            test_wav = alt_wav
        else:
            print(f"  Generating sample audio file...")
            try:
                from engine.alarm_manager import ensure_audio_files
                ensure_audio_files()
            except Exception as e:
                print(f"  Audio generation error: {e}")

    ao = "wasapi" if os.name == "nt" else "pulse"
    print(f"  Launching mpv (--ao={ao}) to play {os.path.basename(test_wav)}...")
    cmd = ["mpv", "--no-video", f"--ao={ao}", "--volume=60", test_wav]
    
    if os.name != "nt":
        bm = BluetoothManager()
        sink = bm.detect_a2dp_sink()
        if sink:
            print(f"  Routing to Bluetooth sink: {sink}")
            cmd.insert(4, f"--audio-device=pulse/{sink}")

    try:
        subprocess.run(cmd, timeout=8.0)
        print("  [OK] Test playback complete!")
    except FileNotFoundError:
        print("  [!] 'mpv' media player not found on system.")
        print("      Install mpv via: sudo apt install mpv")
    except Exception as e:
        print(f"  [!] Audio test error: {e}")

def show_status():
    print("\n" + "=" * 65)
    print("   PALNO & SLEMON — HARDWARE CONNECTION STATUS")
    print("=" * 65)
    bm = BluetoothManager()
    cfg = load_bluetooth_config()
    print(f"  Configured Palno ESP32 MAC:  {cfg.get('palno_mac') or 'Auto-Scan'}")
    print(f"  Configured Speaker MAC:      {cfg.get('speaker_mac') or 'None'}")
    print(f"  Active Audio Output Mode:    {cfg.get('audio_mode', 'bt').upper()}")
    
    spk_status = bm.get_speaker_status()
    print(f"\n  Bluetooth Speaker Status:    {'CONNECTED' if spk_status.get('connected') else 'DISCONNECTED'}")
    print(f"  Active PulseAudio Sink:      {spk_status.get('sink')}")
    print("=" * 65 + "\n")

def main():
    parser = argparse.ArgumentParser(description="Palno & Slemon Bluetooth Connection Assistant")
    parser.add_argument("--scan", action="store_true", help="Scan for Palno ESP32 and Bluetooth speakers")
    parser.add_argument("--speaker", type=str, default="", help="MAC address of Bluetooth A2DP speaker to pair & connect")
    parser.add_argument("--palno", type=str, default="", help="MAC address of Palno ESP32 wearable")
    parser.add_argument("--test-audio", action="store_true", help="Play test audio file through speaker")
    parser.add_argument("--status", action="store_true", help="Show current connection configuration and status")
    args = parser.parse_args()

    if not (args.scan or args.speaker or args.palno or args.test_audio or args.status):
        parser.print_help()
        sys.exit(0)

    if args.status:
        show_status()

    if args.palno:
        set_palno(args.palno)

    if args.speaker:
        connect_speaker(args.speaker)

    if args.scan:
        asyncio.run(scan_ble_palno())
        scan_bluetooth_speakers()

    if args.test_audio:
        test_audio()

if __name__ == "__main__":
    main()
