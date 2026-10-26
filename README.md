# Palno & Slemon: Contactless Dual-Core Circadian Sleep Tracking System

> **Element14 Design Challenge Entry**  
> An advanced dual-core sleep monitoring and circadian wake ecosystem combining contactless mmWave radar sensing, an optical PPG/IMU wearable node, multi-stage machine learning sleep classification, National Instruments LabVIEW clinical telemetry streaming, and automated circadian acoustic intervention.

---

## 📖 System Overview

The **Palno & Slemon** ecosystem bridges high-precision contactless sensing with wearable optical and inertial tracking to achieve clinical-grade sleep quality analysis without compromising user comfort:

1. **Slemon Bedside Station (Arduino Uno Q):**
   - **STM32U585 MCU (Cortex-M33):** Interfaces with high-frequency 60 GHz mmWave radar for non-contact respiration tracking, SHTC3 ambient temperature sensing, and real-time E-ink sleep stage visualization.
   - **Qualcomm QRB2210 Linux MPU (Quad-core A53):** Runs containerized Python microservices orchestrating adaptive baseline respiration detection, 5-stage epoch classification (WAKE, N1, N2, N3, REM), circadian binaural beat synthesis, and an NI LabVIEW TCP broadcast bridge.
   - **RouterBridge RPC:** Inter-processor RPC bridge passing sensor frames and display rendering commands between the real-time MCU and the application MPU.

2. **Palno Wearable Node (ESP32-WROOM DA):**
   - **MAX30102 Optical PPG:** Continuous 100-sample sliding window collecting photoplethysmogram data at 25 SPS for real-time Heart Rate (BPM) and Blood Oxygen Saturation (SpO2 %).
   - **MPU6050 3-Axis IMU:** Samples acceleration and angular velocity at 50 Hz for restlessness scoring and inertial shake-to-dismiss smart alarm dismissal.
   - **BLE GATT Server:** Streams biometric and motion packets at 1 Hz and 20 Hz to Slemon over an isolated BLE GATT link.

3. **Clinician Telemetry & Audio Synthesis:**
   - **NI LabVIEW TCP Socket (Port 5005):** Streams real-time, strictly scalar, unflatten-ready JSON packets formatted for immediate ingestion into LabVIEW Type Clusters.
   - **Interactive Web Dashboard (Port 7000):** Responsive HTML5/Socket.IO application with rolling Chart.js vitals charts, hypnogram progression, and hardware pairing management.
   - **A2DP Bluetooth Audio Speaker:** Automatically pairs with high-fidelity Bluetooth speakers (e.g. ZEBRONICS) to deliver phased sleep-induction soundscapes and gentle circadian wake-up audio.

---

## 🏛 System Architecture

graph TD
    subgraph Slemon["Slemon Bedside Station (Arduino Uno Q)"]
        Radar["60 GHz mmWave FMCW Radar"] -->|UART| STM32["STM32U585 Real-Time MCU"]
        SHTC3["SHTC3 Temp/Humidity"] -->|I2C| STM32
        STM32 -->|SPI| EINK["E-ink Display (Vitals & Stages)"]
        
        STM32 <===>|RouterBridge RPC| LinuxMPU["Qualcomm QRB2210 Linux MPU (Docker)"]
        
        subgraph LinuxEngine["Python App Architecture"]
            BLE_GATT -->|D-Bus Socket Proxy| BLE_Client["engine/ble_client.py (Bleak)"]
            BLE_Client --> AppCore["main.py (SlemonApp Core)"]
            
            AppCore --> Breath["engine/breath_engine.py (Respiration)"]
            AppCore --> StageML["engine/sleep_classifier.py (Epochs)"]
            AppCore --> Alarm["engine/alarm_manager.py (Circadian Audio)"]
            AppCore --> DB["engine/data_logger.py (SQLite WAL)"]
        end
    end

    subgraph Telemetry["Clinician & User Interfaces"]
        AppCore ==>|"TCP Port 5005 (Scalar JSON)"| LabVIEW["NI LabVIEW TCP Telemetry VI"]
        AppCore -->|"WebSocket Port 7000"| WebUI["Responsive Web Dashboard (Chart.js)"]
        Alarm -->|"Bluetooth Classic A2DP"| Speaker["ZEBRONICS A2DP Bluetooth Speaker"]
    end
---

## 🔌 Hardware Bill of Materials (BOM) & Pinout

### 1. Palno Wearable Node (ESP32)
| Component | Function | Interface | ESP32 Pin Connection |
| :--- | :--- | :--- | :--- |
| **ESP32-WROOM DA** | Node Microcontroller & BLE | BLE 4.2 / I2C | — |
| **MAX30102** | Optical PPG (Heart Rate & SpO2) | I2C (Addr: `0x57`) | `SDA -> GPIO 21`, `SCL -> GPIO 22`, `INT -> GPIO 4`, `VIN -> 3.3V` |
| **MPU6050** | 3-Axis Accelerometer & Gyro | I2C (Addr: `0x68`) | `SDA -> GPIO 21`, `SCL -> GPIO 22`, `VCC -> 3.3V`, `GND -> GND` |
| **Status LED** | Heartbeat Indicator | GPIO Output | `GPIO 2` (Onboard LED) |

### 2. Slemon Bedside Hub (Arduino Uno Q)
| Peripheral | Function | Connection |
| :--- | :--- | :--- |
| **Qualcomm QRB2210** | Quad-core A53 Linux MPU (Debian / Docker) | Internal Board Hub |
| **STM32U585** | ARM Cortex-M33 MCU Sensor Co-processor | Internal RouterBridge RPC |
| **60 GHz mmWave Radar** | Contactless Respiration & Movement Energy | `UART TX/RX` to STM32 |
| **SHTC3 Sensor** | Ambient Bedroom Temperature & Humidity | `I2C` to STM32 |
| **E-ink Display** | Low-power Local Sleep Vitals Display | `SPI` to STM32 |
| **ZEBRONICS Speaker** | Binaural Beat Alarm & Induction Audio | Bluetooth Classic A2DP (`30:21:44:62:2C:58`) |

---

## 📂 Repository Structure

```text
├── Palno/                             # ESP32 Wearable Node Source Code
│   └── palno_esp32/
│       ├── palno_esp32.ino            # Main Arduino setup & multi-rate scheduling
│       ├── ble_service.h              # BLE GATT server, UUIDs & Notify callbacks
│       ├── biometrics.h               # MAX30102 sliding window & outlier filter
│       ├── motion.h                   # MPU6050 burst reads & shake detector
│       ├── algorithm.h / .cpp         # Maxim optical pulse oximetry algorithm
│       └── max30102.h / .cpp          # Register-level I2C MAX30102 driver
│
├── sketch/                            # Arduino Uno Q STM32 MCU Firmware
│   ├── sketch.ino                     # STM32 FreeRTOS sensor polling & display RPC
│   ├── radar_parser.h                 # 60 GHz mmWave radar packet decoder
│   ├── eink_display.h                 # E-ink layout renderer (Sleep stages, vitals)
│   ├── temp_sensor.h                  # SHTC3 temperature & humidity driver
│   └── sketch.yaml                    # Arduino CLI build manifest
│
├── python/                            # Arduino Uno Q Linux MPU Application
│   ├── main.py                        # SlemonApp Core, WebUI & 10 Hz Telemetry Worker
│   ├── requirements.txt               # Dependencies (bleak, dbus-fast, fastapi)
│   └── engine/
│       ├── ble_client.py              # Asynchronous BLE GATT client with auto-reconnect
│       ├── breath_engine.py           # Adaptive baseline mmWave respiration extractor
│       ├── sleep_classifier.py        # 30-second epoch sleep stage classifier (WAKE/N1/N2/N3/REM)
│       ├── labview_bridge.py          # 10 Hz NI LabVIEW TCP broadcast socket server
│       ├── alarm_manager.py           # Circadian sleep-induction & binaural beat audio
│       ├── bluetooth_manager.py       # Host D-Bus & BlueZ pairing orchestrator
│       ├── data_logger.py             # SQLite WAL database & CSV exporter
│       ├── serial_bridge.py           # Fallback serial telemetry bridge
│       └── config.py                  # System thresholds, MACs & tuning parameters
│
├── assets/                            # Responsive Web Dashboard (Port 7000)
│   ├── index.html                     # Clean dark-mode dashboard UI
│   ├── style.css                      # Modern responsive CSS grid & animations
│   ├── app.js                         # Socket.IO telemetry subscriber & Chart.js renderer
│   ├── libs/                          # Local JS libraries (socket.io, arduino.js)
│   └── fonts/                         # Offline Roboto & Open Sans web fonts
│
├── audio/                             # Binaural & Circadian Soundscapes
│   ├── sleep_induction_delta.wav      # 0.5-4 Hz delta waves for deep sleep induction
│   └── wake_circadian_alpha.wav       # 8-12 Hz alpha waves for gentle awakening
│
├── slemon_startup.sh                  # Host boot script (Bluetooth power, D-Bus, port forwards)
├── bt_host_agent.py                   # Host-level Bluetooth Classic audio management service
├── app.yaml                           # Arduino Uno Q App Lab container configuration
├── LICENSE                            # Open Source License (Apache 2.0 / MIT)
└── README.md                          # Comprehensive project documentation
```

---

## 🚀 Setup & Installation Guide

### Step 1: Flash the Palno Wearable Node (ESP32)
1. Open the [Arduino IDE](https://www.arduino.cc/en/software).
2. Install the **ESP32 by Espressif Systems** board package (`Tools -> Board -> Boards Manager`).
3. Open [`Palno/palno_esp32/palno_esp32.ino`](Palno/palno_esp32/palno_esp32.ino).
4. Select your ESP32 board and COM port.
5. Compile and upload. The board will begin advertising as `Palno-ESP32` (`UUID: a1b2c3d4-e5f6-7890-abcd-ef1234567890`).

### Step 2: Deploy to Arduino Uno Q via App Lab
1. Compress the project folder into a ZIP file:
   - Ensure `app.yaml`, `python/`, `assets/`, and `sketch/` are in the root of the archive.
2. Open the **Arduino App Lab** web interface on your local network:
   ```text
   http://uno-q.local/ or http://192.168.1.12/
   ```
3. Upload the ZIP package. App Lab compiles the STM32 sketch onto the real-time MCU and spins up the Python container on the Qualcomm MPU.

### Step 3: Zero-Touch Boot Configuration (Linux Host)
The system automatically orchestrates all dependencies across reboots without requiring any terminal intervention:
- **`dbus-proxy.service`**: Dynamically binds the Linux D-Bus socket into the active container for Bleak BLE access.
- **`labview-forward.service`**: Dynamically discovers the active container IP and forwards TCP port `5005` to the host.
- **`bt-host-agent.service`**: Background daemon handling Classic Bluetooth A2DP pairing for the audio speaker.
- **`slemon-startup.service`**: Runs [`slemon_startup.sh`](slemon_startup.sh) on boot to power up Bluetooth, verify container restart policies, and trigger speaker auto-connection.

---

## 📊 NI LabVIEW Integration

The Slemon station broadcasts live telemetry on **TCP Port 5005** at 10 Hz formatted as a clean, single-line UTF-8 scalar JSON string terminated by `\r\n`.

### Connecting in LabVIEW:
1. Place **`TCP Open Connection.vi`** on your block diagram.
   - **Address:** `127.0.0.1` (via USB / ADB) or `192.168.1.12` (via Wi-Fi)
   - **Port:** `5005`
2. In a While Loop, use **`TCP Read.vi`** with `mode = 1` (CRLF / standard line read).
3. Wire the incoming string to **`Unflatten From JSON.vi`**.
4. Wire a Type Cluster containing the exact fields below:

```json
{
  "ts": 1792931468.36,
  "breath_bpm": 15.0,
  "hr_bpm": 71,
  "heart_rate": 71,
  "hr_valid": 1,
  "spo2": 98,
  "spo2_valid": 1,
  "room_temp_c": 28.27,
  "temp_c": 28.27,
  "disturbance": 9.2,
  "sleep_stage": "WAKE",
  "stage": "WAKE",
  "sleep_stage_numeric": 4,
  "alarm_active": false,
  "ble_connected": true,
  "sleep_score": 85,
  "radar_mov_dist": 69.0,
  "radar_mov_energy": 0.0,
  "radar_stat_energy": 100.0,
  "accel_x": -0.125,
  "accel_y": 0.222,
  "accel_z": 0.981
}
```

> [!TIP]
> **Numeric Sleep Stage Mapping:**  
> `0 = N3 (Deep Sleep)` | `1 = N2 (Light Sleep)` | `2 = REM (Dream Sleep)` | `3 = N1 (Dozing)` | `4 = WAKE`

---

## ⏰ Circadian Smart Alarm & Shake-to-Dismiss

- **Circadian Wake Window:** The user sets a target wake time (e.g. `07:00`) and window (e.g. `30 min`). Slemon evaluates sleep stages in real-time, activating the gentle wake alarm only when the user transitions into light sleep (N1/N2) within the window.
- **Inertial Shake Dismissal:** To eliminate morning button-fumbling, the alarm requires **10 vigorous hand shakes** (`MPU6050 dyn_accel > 0.40 G`). The system counts shakes in parallel on both the ESP32 and the Uno Q motion engine, instantly silencing the alarm upon reaching 10 shakes.

---

## 📄 License & Acknowledgments

- Developed for the **Element14 Design Challenge 2026**.
- Open source under the [GNU General Public License v3.0].
- Includes the Maxim Integrated MAX30102 pulse oximetry algorithm.
