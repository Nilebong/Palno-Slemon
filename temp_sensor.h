/*******************************************************************************
 * temp_sensor.h — MAX30208 Temperature Sensor I2C Wrapper for Slemon (STM32)
 *
 * Implements the exact read workflow from temperature.ino (Inspiration/):
 *   1. Write 0xC1 to register 0x14 → trigger single-shot conversion
 *   2. Wait 20 ms for conversion to complete
 *   3. Point register pointer to FIFO data register 0x08
 *   4. Read 2 bytes (MSB, LSB), reconstruct signed int16, multiply by 0.005
 *
 * Key specs:
 *   I2C address: 0x50 (default)
 *   Resolution:  0.005 °C per LSB
 *   Range:       −40 °C to +125 °C
 *   Conversion:  ~15 ms typical, 20 ms safe margin
 *   Update rate: every 10 seconds (temperature changes slowly)
 *
 * The 20 ms blocking delay is acceptable on the STM32 co-processor since
 * temperature is read only every 10 s (all other I2C operations are paused
 * for this 20 ms window, which is negligible).
 ******************************************************************************/
#ifndef TEMP_SENSOR_H_
#define TEMP_SENSOR_H_

#include <Arduino.h>
#include <Wire.h>

// ─── MAX30208 register map ───────────────────────────────────────────────────
#define MAX30208_I2C_ADDR    0x50
#define MAX30208_REG_STATUS  0x00
#define MAX30208_REG_FIFO    0x08   // FIFO temperature data register
#define MAX30208_REG_SETUP   0x14   // Temperature setup / CONVERT_T trigger
#define MAX30208_REG_PART_ID 0xFF   // Expected: 0x30

// ─── Update interval ─────────────────────────────────────────────────────────
#define TEMP_UPDATE_INTERVAL_MS  10000UL   // 10 seconds

// ─── State ───────────────────────────────────────────────────────────────────
static float    temp_last_reading_c = -999.0f;
static bool     temp_sensor_ok      = false;
static uint32_t temp_last_update_ms = 0;

// ─── Verify sensor presence ──────────────────────────────────────────────────
/**
 * Reads the Part ID register and verifies it equals 0x30.
 * Call from setup() after Wire.begin().
 * Returns true if sensor is present and responding.
 */
inline bool temp_verify() {
  Wire.beginTransmission(MAX30208_I2C_ADDR);
  Wire.write(MAX30208_REG_PART_ID);
  if (Wire.endTransmission(false) != 0) {
    Serial.println("[TEMP] ERROR: MAX30208 not found on I2C bus (addr 0x50)");
    temp_sensor_ok = false;
    return false;
  }

  Wire.requestFrom((uint8_t)MAX30208_I2C_ADDR, (uint8_t)1);
  if (Wire.available()) {
    uint8_t partID = Wire.read();
    if (partID == 0x30) {
      Serial.println("[TEMP] MAX30208 detected OK (Part ID = 0x30)");
      temp_sensor_ok = true;
      return true;
    } else {
      Serial.print("[TEMP] WARNING: Unexpected Part ID = 0x");
      Serial.println(partID, HEX);
      // Still allow operation — may be a revision difference
      temp_sensor_ok = true;
      return true;
    }
  }

  Serial.println("[TEMP] ERROR: No response from MAX30208");
  temp_sensor_ok = false;
  return false;
}

// ─── Single blocking temperature read ───────────────────────────────────────
/**
 * Triggers a single-shot conversion, waits 20 ms, then reads 2 bytes.
 * BLOCKING for 20 ms. Only call from the 10-second timer slot.
 * Returns temperature in °C, or -999.0 on error.
 */
static float temp_read_blocking() {
  // Step 1: Trigger single-shot conversion
  Wire.beginTransmission(MAX30208_I2C_ADDR);
  Wire.write(MAX30208_REG_SETUP);
  Wire.write(0xC1);    // CONVERT_T bit + default settings
  if (Wire.endTransmission() != 0) {
    return -999.0f;
  }

  // Step 2: Wait for conversion (~15 ms typical, 20 ms safe margin)
  delay(20);

  // Step 3: Point register pointer to FIFO data
  Wire.beginTransmission(MAX30208_I2C_ADDR);
  Wire.write(MAX30208_REG_FIFO);
  if (Wire.endTransmission(false) != 0) {
    return -999.0f;
  }

  // Step 4: Read 2 bytes (MSB first)
  Wire.requestFrom((uint8_t)MAX30208_I2C_ADDR, (uint8_t)2);
  if (Wire.available() == 2) {
    uint8_t msb = Wire.read();
    uint8_t lsb = Wire.read();

    // Reconstruct signed 16-bit 2's complement value
    int16_t rawTemp = (int16_t)((uint16_t)(msb << 8) | lsb);

    // Convert to °C: each LSB = 0.005 °C
    return rawTemp * 0.005f;
  }

  return -999.0f;
}

// ─── Initialise ──────────────────────────────────────────────────────────────
/**
 * Call once from setup() after Wire.begin().
 * Verifies sensor presence and takes an initial reading.
 */
inline void temp_init() {
  if (!temp_verify()) return;

  // Take initial reading immediately
  temp_last_reading_c = temp_read_blocking();
  temp_last_update_ms = millis();

  if (temp_last_reading_c != -999.0f) {
    Serial.print("[TEMP] Initial reading: ");
    Serial.print(temp_last_reading_c, 3);
    Serial.println(" °C");
  } else {
    Serial.println("[TEMP] Initial read failed");
  }
}

// ─── Non-blocking periodic update ────────────────────────────────────────────
/**
 * Call every loop(). Only triggers a real I2C read every TEMP_UPDATE_INTERVAL_MS.
 * Returns true when a new reading has been taken.
 */
inline bool temp_update() {
  if (!temp_sensor_ok) return false;

  uint32_t now = millis();
  if (now - temp_last_update_ms < TEMP_UPDATE_INTERVAL_MS) return false;

  temp_last_update_ms = now;
  float newTemp = temp_read_blocking();

  if (newTemp != -999.0f) {
    temp_last_reading_c = newTemp;
    return true;
  }
  return false;
}

// ─── Getters ────────────────────────────────────────────────────────────────
inline float temp_get_celsius()   { return temp_last_reading_c; }
inline bool  temp_is_valid()      { return temp_sensor_ok && (temp_last_reading_c != -999.0f); }

#endif /* TEMP_SENSOR_H_ */
