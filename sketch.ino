/*******************************************************************************
 * sketch.ino — Slemon Arduino Uno Q STM32 Co-Processor Firmware
 *
 * Part of the Palno & Slemon Sleep Tracking System.
 * Runs on the STM32U585 MCU (Zephyr platform).
 * Communicates with Qualcomm QRB2210 MPU via Arduino_RouterBridge.
 *
 * Connected Hardware:
 *   1. HLK-LD2410 mmWave Radar:
 *      - Connected to Serial2 at 256,000 baud
 *      - Non-blocking binary FSM parser with engineering mode
 *      - Streams 22-field telemetry (states, distance, energies, gates)
 *   2. MAX30208 Precision Temperature Sensor:
 *      - I2C address 0x50
 *      - Read every 10 seconds (0.005 °C/LSB resolution)
 *   3. 3.5" 4-Colour E-ink Display (GDEM035F51):
 *      - GxEPD2_4C SPI interface
 *      - Pin config: CS=4, DC=16, RST=17, BUSY=15, PWR=25
 *      - Screens: IDLE, SLEEP_ACTIVE, ALARM, MORNING_SUMMARY
 *
 * Bridge RPC endpoints provided to Qualcomm Python side:
 *   - get_radar_json()    -> String (JSON serialization of all radar fields)
 *   - get_temp()          -> float (room temperature in °C)
 *   - show_display()      -> void (update e-ink display screen)
 *   - set_alarm_active()  -> void (enable/disable alarm flashing LED)
 ******************************************************************************/

#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>
#include <Arduino_RouterBridge.h>

// On Arduino Uno Q, map standard Serial logging to Monitor so output appears
// in the Arduino App Lab Serial Monitor
#define Serial Monitor

#include "radar_parser.h"
#include "temp_sensor.h"
#include "eink_display.h"

// ─── Alarm indicator state ───────────────────────────────────────────────────
static bool     is_alarm_active      = false;
static uint32_t last_alarm_toggle_ms = 0;
static bool     alarm_led_state      = false;

// ─── Simple JSON string parser helpers ───────────────────────────────────────
static String json_extract_string(const String& json, const String& key) {
  String search = "\"" + key + "\":";
  int idx = json.indexOf(search);
  if (idx < 0) return "";
  idx += search.length();
  while (idx < (int)json.length() && (json[idx] == ' ' || json[idx] == '\"')) {
    idx++;
  }
  int endIdx = idx;
  while (endIdx < (int)json.length() && json[endIdx] != '\"' && json[endIdx] != ',' && json[endIdx] != '}') {
    endIdx++;
  }
  return json.substring(idx, endIdx);
}

static float json_extract_float(const String& json, const String& key, float defaultVal = 0.0f) {
  String val = json_extract_string(json, key);
  if (val.length() == 0) return defaultVal;
  return val.toFloat();
}

static int json_extract_int(const String& json, const String& key, int defaultVal = 0) {
  String val = json_extract_string(json, key);
  if (val.length() == 0) return defaultVal;
  return val.toInt();
}

static bool json_extract_bool(const String& json, const String& key, bool defaultVal = false) {
  String val = json_extract_string(json, key);
  if (val.equalsIgnoreCase("true") || val == "1") return true;
  if (val.equalsIgnoreCase("false") || val == "0") return false;
  return defaultVal;
}

// ─── Bridge RPC: Get Radar Telemetry JSON ────────────────────────────────────
String get_radar_json() {
  const RadarTelemetry& d = radar_get_data();

  String s = "{";
  s += "\"valid\":" + String(d.valid ? "true" : "false") + ",";
  s += "\"ts\":" + String(d.frame_ts_ms) + ",";
  s += "\"target_state\":" + String(d.target_state) + ",";
  s += "\"mov_dist_cm\":" + String(d.mov_dist_cm) + ",";
  s += "\"mov_energy\":" + String(d.mov_energy) + ",";
  s += "\"sta_dist_cm\":" + String(d.sta_dist_cm) + ",";
  s += "\"sta_energy\":" + String(d.sta_energy) + ",";
  s += "\"det_dist_cm\":" + String(d.det_dist_cm) + ",";
  s += "\"mg\":[";
  for (int i = 0; i < 9; i++) {
    s += String(d.mg[i]);
    if (i < 8) s += ",";
  }
  s += "],\"sg\":[";
  for (int i = 0; i < 6; i++) {
    s += String(d.sg[i]);
    if (i < 5) s += ",";
  }
  s += "]}";

  return s;
}

// ─── Bridge RPC: Get Room Temperature ────────────────────────────────────────
float get_temp() {
  return temp_get_celsius();
}

// ─── Bridge RPC: Set Alarm Mode ──────────────────────────────────────────────
void set_alarm_active(bool active) {
  is_alarm_active = active;
  if (!active) {
#ifdef LED4_R
    digitalWrite(LED4_R, HIGH);  // Active-low: HIGH turns LED off
#endif
#ifdef LED_BUILTIN
    digitalWrite(LED_BUILTIN, LOW);
#endif
  }
  Serial.print("[ALARM] Active set to: ");
  Serial.println(active ? "TRUE" : "FALSE");
}

// ─── Bridge RPC: Show Screen on E-ink Display ────────────────────────────────
void show_display(String type, String json_data) {
  Serial.print("[EINK] Display request: ");
  Serial.println(type);

  if (type.equalsIgnoreCase("SLEEP") || type.equalsIgnoreCase("SLEEP_ACTIVE")) {
    float breathBpm = json_extract_float(json_data, "bpm", 0.0f);
    int32_t hr      = json_extract_int(json_data, "hr", 0);
    int32_t spo2    = json_extract_int(json_data, "spo2", 0);
    float tempC     = json_extract_float(json_data, "temp", temp_get_celsius());
    String stage    = json_extract_string(json_data, "stage");
    if (stage.length() == 0) stage = "N2";
    String timeStr  = json_extract_string(json_data, "time");
    if (timeStr.length() == 0) timeStr = "--:--";

    bool hrValid   = json_extract_bool(json_data, "hr_valid", hr > 0);
    bool spo2Valid = json_extract_bool(json_data, "spo2_valid", spo2 > 0);

    eink_show_sleep_active(breathBpm, hr, spo2, tempC, stage.c_str(), timeStr.c_str(), hrValid, spo2Valid);
  }
  else if (type.equalsIgnoreCase("ALARM") || type.equalsIgnoreCase("ALARM_RINGING")) {
    String timeStr = json_extract_string(json_data, "time");
    if (timeStr.length() == 0) timeStr = "07:00";
    int sleepScore = json_extract_int(json_data, "score", 85);

    eink_show_alarm(timeStr.c_str(), sleepScore);
  }
  else if (type.equalsIgnoreCase("SUMMARY") || type.equalsIgnoreCase("MORNING_SUMMARY")) {
    uint16_t duration_min = (uint16_t)json_extract_int(json_data, "duration_min", 480);
    int sleepScore        = json_extract_int(json_data, "score", 85);
    float avgBreath       = json_extract_float(json_data, "avg_breath", 14.5f);
    int32_t avgHR         = (int32_t)json_extract_int(json_data, "avg_hr", 65);
    int32_t avgSpo2       = (int32_t)json_extract_int(json_data, "avg_spo2", 98);
    float minTempC        = json_extract_float(json_data, "min_temp", 20.0f);
    float maxTempC        = json_extract_float(json_data, "max_temp", 22.0f);
    uint8_t cycleCount    = (uint8_t)json_extract_int(json_data, "cycles", 5);

    String hypnoStr       = json_extract_string(json_data, "hypnogram");

    eink_show_morning_summary(duration_min, sleepScore, avgBreath, avgHR, avgSpo2, minTempC, maxTempC, cycleCount, hypnoStr.c_str());
  }
  else if (type.equalsIgnoreCase("IDLE")) {
    String ipAddr = json_extract_string(json_data, "ip");
    if (ipAddr.length() == 0) ipAddr = "192.168.1.50";
    bool bleConn  = json_extract_bool(json_data, "ble", false);

    eink_show_idle(ipAddr.c_str(), bleConn, true, temp_is_valid());
  }
}

// ─── Arduino Setup ───────────────────────────────────────────────────────────
void setup() {
  // 1. Initialise Router Bridge to Qualcomm MPU and App Lab Monitor Console
  Bridge.begin();
  Monitor.begin(115200);

  // Wait briefly (up to 2s) for monitor connection; never blocks if closed
  uint32_t tStart = millis();
  while (!Monitor && (millis() - tStart < 2000)) {
    delay(10);
  }

  Serial.println();
  Serial.println("==================================================");
  Serial.println("  Slemon Arduino Uno Q STM32 Co-Processor");
  Serial.println("==================================================");

#ifdef LED4_R
  pinMode(LED4_R, OUTPUT);
  digitalWrite(LED4_R, HIGH);  // OFF
#endif
#ifdef LED_BUILTIN
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);
#endif

  // 2. Scan and verify I2C bus on Uno Q (D20=SCL, D21=SDA)
  Wire.begin();
  Serial.println("[I2C] Scanning I2C bus (D20=SCL, D21=SDA)...");
  byte count = 0;
  for (byte addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
      Serial.print("[I2C] Device found at 0x");
      if (addr < 16) Serial.print("0");
      Serial.print(addr, HEX);
      if (addr == 0x50) {
        Serial.print("  <-- MAX30208 Temperature Sensor detected!");
      }
      Serial.println();
      count++;
    }
  }
  if (count == 0) {
    Serial.println("[I2C] WARNING: No I2C devices found!");
    Serial.println("[I2C] Check wiring: Clock -> D20 (SCL), Data -> D21 (SDA), 3.3V, GND.");
  }

  // Initialise MAX30208 Temperature sensor logic
  temp_init();

  // 3. Initialise LD2410 mmWave Radar over Serial1 (Pins D0=RX, D1=TX)
  // On Uno Q: D0 = RX (Radar TX), D1 = TX (Radar RX) at 256,000 baud
  Serial.println("[RADAR] Initialising radar on Serial1 (D0=RX, D1=TX, 256000 baud)...");
  radar_init(&Serial1, 256000);
  delay(100);
  radar_enable_engineering_mode();
  Serial.println("[RADAR] Engineering mode command sent.");

  // 4. Initialise E-ink Display (shows splash screen)
  Serial.println("[EINK] Initialising e-ink display...");
  eink_init();

  // 5. Expose RPC functions to Qualcomm side
  Bridge.provide("get_radar_json", get_radar_json);
  Bridge.provide("get_radar", get_radar_json);
  Bridge.provide("get_temp", get_temp);
  Bridge.provide("get_temperature", get_temp);
  Bridge.provide("show_display", show_display);
  Bridge.provide("set_alarm_active", set_alarm_active);
  Bridge.provide("set_alarm_mode", set_alarm_active);

  Serial.println("[SETUP] Slemon STM32 ready and listening for Python Bridge RPC!");
  Serial.println("==================================================");
}

// ─── Arduino Main Loop ───────────────────────────────────────────────────────
void loop() {
  // Non-blocking radar FSM update from Serial1 buffer
  radar_update();

  // Periodic temperature update (triggers read every 10 seconds)
  temp_update();

  // Periodic debug telemetry to Serial Monitor (every 2 seconds)
  static uint32_t lastDbgMs = 0;
  uint32_t now = millis();
  if (now - lastDbgMs >= 2000) {
    lastDbgMs = now;
    const RadarTelemetry& d = radar_get_data();
    Serial.print("[STATUS] Radar: valid=");
    Serial.print(d.valid ? "YES" : "NO");
    Serial.print(" state=");
    Serial.print(d.target_state);
    Serial.print(" movE=");
    Serial.print(d.mov_energy);
    Serial.print(" dist=");
    Serial.print(d.mov_dist_cm);
    Serial.print("cm | Temp: ");
    float t = temp_get_celsius();
    if (t > -100.0f) {
      Serial.print(t, 2);
      Serial.println(" C");
    } else {
      Serial.println("DISCONNECTED (-999 C - Check Clock->D20, SDA->D21)");
    }
  }

  // Visual alarm flashing (50% duty cycle, 2 Hz)
  if (is_alarm_active) {
    if (now - last_alarm_toggle_ms >= 250) {
      last_alarm_toggle_ms = now;
      alarm_led_state = !alarm_led_state;
#ifdef LED4_R
      digitalWrite(LED4_R, alarm_led_state ? LOW : HIGH);
#endif
#ifdef LED_BUILTIN
      digitalWrite(LED_BUILTIN, alarm_led_state ? HIGH : LOW);
#endif
    }
  }

  // Yield to allow background RTOS tasks to execute
  yield();
}
