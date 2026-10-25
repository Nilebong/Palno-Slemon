/*******************************************************************************
 * radar_parser.h — LD2410 Binary Packet Parser for Slemon (STM32)
 *
 * The HLK-LD2410B mmWave radar streams binary data at 256,000 baud.
 * This module implements a non-blocking finite-state-machine that:
 *
 *   1. Syncs on the 4-byte frame header:  0xF4 0xF3 0xF2 0xF1
 *   2. Parses the data-frame body:
 *        [2B length] [0x02 type] [0xAA head] [data...] [0x55 0x00 tail] [0xF8..0xF5 footer]
 *   3. Extracts telemetry: target state, moving/static distance & energy,
 *        detection distance, and 9+6 gate energies
 *   4. Recognises ACK frames (header 0xFD 0xFC 0xFB 0xFA) for command responses
 *
 * Usage:
 *   radar_init(&Serial2, 256000);    // call in setup()
 *   radar_update();                  // call every loop() — non-blocking
 *   if (radar_data_ready()) { ... }  // check for fresh parsed frame
 *
 * Wire:
 *   LD2410 TX → STM32 RX2 (hardware serial)
 *   LD2410 RX ← STM32 TX2
 *   Baud: 256000, 3.3 V logic
 ******************************************************************************/
#ifndef RADAR_PARSER_H_
#define RADAR_PARSER_H_

#include <Arduino.h>

// ─── Frame headers ──────────────────────────────────────────────────────────
static const uint8_t DATA_FRAME_HDR[4]  = { 0xF4, 0xF3, 0xF2, 0xF1 };
static const uint8_t ACK_FRAME_HDR[4]   = { 0xFD, 0xFC, 0xFB, 0xFA };
static const uint8_t DATA_FRAME_FTR[4]  = { 0xF8, 0xF7, 0xF6, 0xF5 };
static const uint8_t ACK_FRAME_FTR[4]   = { 0x04, 0x03, 0x02, 0x01 };

// ─── Parser state machine ────────────────────────────────────────────────────
typedef enum {
  RADAR_STATE_HUNT_HEADER = 0,  // scanning for sync bytes
  RADAR_STATE_READ_LEN,         // reading 2-byte data length
  RADAR_STATE_READ_BODY,        // reading body bytes
  RADAR_STATE_HUNT_FOOTER       // scanning for 4-byte footer
} RadarFsmState;

// ─── Parsed telemetry (populated after each valid data frame) ────────────────
typedef struct {
  // Target detection state: 0=no target, 1=moving, 2=static, 3=both
  uint8_t  target_state;

  // Moving target
  uint16_t mov_dist_cm;     // moving target distance (cm)
  uint8_t  mov_energy;      // moving target energy (0–100)

  // Static target
  uint16_t sta_dist_cm;     // static target distance (cm)
  uint8_t  sta_energy;      // static target energy (0–100)

  // Detection range
  uint16_t det_dist_cm;     // maximum detection distance (cm)

  // Gate moving energies [0..8]  (9 gates)
  uint8_t  mg[9];

  // Gate static energies [0..8]  (used: 3..8 → indices 0..5)
  uint8_t  sg[6];

  // Validity flag — set true when a complete frame is successfully parsed
  bool     valid;
  uint32_t frame_ts_ms;     // millis() when this frame was completed
} RadarTelemetry;

// ─── Globals ────────────────────────────────────────────────────────────────
static HardwareSerial*  radarSerial   = nullptr;
static RadarFsmState    radarFsmState = RADAR_STATE_HUNT_HEADER;
static RadarTelemetry   radarData;
static bool             radarDataReady = false;

// Internal parse buffer
static uint8_t  radarBuf[128];
static uint16_t radarBufIdx     = 0;
static uint16_t radarFrameLen   = 0;
static uint8_t  radarHdrMatch   = 0;   // how many header bytes matched
static bool     radarIsAckFrame = false;

// ─── Initialisation ──────────────────────────────────────────────────────────
/**
 * @param serial   Pointer to HardwareSerial connected to LD2410 UART.
 * @param baud     Must be 256000 for LD2410.
 */
inline void radar_init(HardwareSerial* serial, uint32_t baud = 256000) {
  radarSerial = serial;
  radarSerial->begin(baud);
  memset(&radarData, 0, sizeof(radarData));
  radarFsmState  = RADAR_STATE_HUNT_HEADER;
  radarDataReady = false;
  radarHdrMatch  = 0;
  radarBufIdx    = 0;
  Serial.println("[RADAR] Parser initialised (256000 baud)");
}

// ─── Internal: parse a complete body buffer into radarData ──────────────────
static void radar_parse_data_body(const uint8_t* body, uint16_t len) {
  // Minimum useful body: [0x02 type][0xAA head][data...][0x55][0x00]
  // Byte layout (0-indexed within body):
  //   [0]    = data type (0x02 for engineering mode extended)
  //   [1]    = head marker 0xAA
  //   [2]    = target state
  //   [3-4]  = moving target distance (LE uint16)
  //   [5]    = moving target energy
  //   [6-7]  = static target distance (LE uint16)
  //   [8]    = static target energy
  //   [9-10] = detection distance (LE uint16)
  //   [11-19]= moving gate energies MG0..MG8
  //   [20-25]= static gate energies SG3..SG8
  //   [26]   = tail 0x55
  //   [27]   = tail 0x00

  if (len < 26) return;   // too short
  if (body[0] != 0x02 && body[0] != 0x01) return;  // unexpected type byte
  if (body[1] != 0xAA) return;  // missing head marker

  radarData.target_state = body[2];
  radarData.mov_dist_cm  = (uint16_t)(body[3] | (body[4] << 8));
  radarData.mov_energy   = body[5];
  radarData.sta_dist_cm  = (uint16_t)(body[6] | (body[7] << 8));
  radarData.sta_energy   = body[8];
  radarData.det_dist_cm  = (uint16_t)(body[9] | (body[10] << 8));

  // Engineering mode gate energies (only in 0x02 type frames)
  if (body[0] == 0x02 && len >= 28) {
    for (int i = 0; i < 9; i++) radarData.mg[i] = body[11 + i];
    for (int i = 0; i < 6; i++) radarData.sg[i] = body[20 + i];
  }

  radarData.valid        = true;
  radarData.frame_ts_ms  = millis();
  radarDataReady         = true;
}

// ─── Update (call every loop iteration) ─────────────────────────────────────
/**
 * Non-blocking. Drains whatever bytes are in the serial RX buffer and
 * advances the state machine. May parse 0 or many frames per call.
 */
inline void radar_update() {
  if (radarSerial == nullptr) return;

  while (radarSerial->available() > 0) {
    uint8_t byte = (uint8_t)radarSerial->read();

    switch (radarFsmState) {

      // ── HUNT: look for either DATA (0xF4...) or ACK (0xFD...) header ──
      case RADAR_STATE_HUNT_HEADER:
        if (radarHdrMatch < 4) {
          // Try DATA frame header
          if (byte == DATA_FRAME_HDR[radarHdrMatch]) {
            radarHdrMatch++;
            radarIsAckFrame = false;
            if (radarHdrMatch == 4) {
              radarFsmState = RADAR_STATE_READ_LEN;
              radarBufIdx   = 0;
              radarHdrMatch = 0;
            }
          }
          // Try ACK frame header
          else if (byte == ACK_FRAME_HDR[radarHdrMatch]) {
            radarHdrMatch++;
            radarIsAckFrame = true;
            if (radarHdrMatch == 4) {
              radarFsmState = RADAR_STATE_READ_LEN;
              radarBufIdx   = 0;
              radarHdrMatch = 0;
            }
          }
          else {
            // Mismatch — restart header search
            radarHdrMatch = 0;
            radarIsAckFrame = false;
            // Check if this byte starts a new header
            if (byte == DATA_FRAME_HDR[0])      { radarHdrMatch = 1; radarIsAckFrame = false; }
            else if (byte == ACK_FRAME_HDR[0])  { radarHdrMatch = 1; radarIsAckFrame = true;  }
          }
        }
        break;

      // ── READ_LEN: capture 2-byte little-endian data length ──
      case RADAR_STATE_READ_LEN:
        radarBuf[radarBufIdx++] = byte;
        if (radarBufIdx == 2) {
          radarFrameLen = (uint16_t)(radarBuf[0] | (radarBuf[1] << 8));
          radarBufIdx   = 0;
          if (radarFrameLen == 0 || radarFrameLen > (uint16_t)(sizeof(radarBuf) - 4)) {
            // Invalid length — go back to hunting
            radarFsmState = RADAR_STATE_HUNT_HEADER;
            radarHdrMatch = 0;
          } else {
            radarFsmState = RADAR_STATE_READ_BODY;
          }
        }
        break;

      // ── READ_BODY: collect exactly radarFrameLen bytes ──
      case RADAR_STATE_READ_BODY:
        radarBuf[radarBufIdx++] = byte;
        if (radarBufIdx >= radarFrameLen) {
          // Body complete — now hunt for 4-byte footer
          radarFsmState = RADAR_STATE_HUNT_FOOTER;
          radarHdrMatch = 0;
        }
        break;

      // ── HUNT_FOOTER: verify the 4-byte footer then process ──
      case RADAR_STATE_HUNT_FOOTER: {
        const uint8_t* expectedFtr = radarIsAckFrame ? ACK_FRAME_FTR : DATA_FRAME_FTR;
        if (byte == expectedFtr[radarHdrMatch]) {
          radarHdrMatch++;
          if (radarHdrMatch == 4) {
            // Complete valid frame
            if (!radarIsAckFrame) {
              radar_parse_data_body(radarBuf, radarBufIdx);
            }
            // Reset for next frame
            radarFsmState = RADAR_STATE_HUNT_HEADER;
            radarHdrMatch = 0;
            radarBufIdx   = 0;
          }
        } else {
          // Footer mismatch — go back to hunt
          radarFsmState = RADAR_STATE_HUNT_HEADER;
          radarHdrMatch = 0;
          radarBufIdx   = 0;
        }
        break;
      }

      default:
        radarFsmState = RADAR_STATE_HUNT_HEADER;
        radarHdrMatch = 0;
        break;
    }
  }
}

// ─── Data accessors ──────────────────────────────────────────────────────────
/** Returns true and clears the flag when a fresh frame has been parsed. */
inline bool radar_data_ready() {
  if (radarDataReady) {
    radarDataReady = false;
    return true;
  }
  return false;
}

/** Returns a const reference to the latest parsed telemetry. */
inline const RadarTelemetry& radar_get_data() {
  return radarData;
}

// ─── Enable engineering mode (call after init to stream gate energies) ───────
/**
 * Sends the LD2410 command to enable engineering mode so gate energies
 * appear in data frames.
 *
 * Command sequence:
 *   Header: FD FC FB FA
 *   [len LE] [0x04 0x00]   → len = 4
 *   [0xFF 0x00]            → command word: enable engineering mode
 *   [0x02 0x00]            → value
 *   Footer: 04 03 02 01
 */
inline void radar_enable_engineering_mode() {
  if (radarSerial == nullptr) return;

  // Config enable: FD FC FB FA [04 00] [FF 00] [02 00] 04 03 02 01
  const uint8_t cmd_enable_cfg[] = {
    0xFD, 0xFC, 0xFB, 0xFA,
    0x04, 0x00,
    0xFF, 0x00,
    0x01, 0x00,
    0x04, 0x03, 0x02, 0x01
  };
  radarSerial->write(cmd_enable_cfg, sizeof(cmd_enable_cfg));
  delay(50);

  // Engineering mode enable: FD FC FB FA [04 00] [62 00] [01 00] 04 03 02 01
  const uint8_t cmd_eng_mode[] = {
    0xFD, 0xFC, 0xFB, 0xFA,
    0x04, 0x00,
    0x62, 0x00,
    0x01, 0x00,
    0x04, 0x03, 0x02, 0x01
  };
  radarSerial->write(cmd_eng_mode, sizeof(cmd_eng_mode));
  delay(50);

  // Config disable (end command session): FD FC FB FA [02 00] [FE 00] 04 03 02 01
  const uint8_t cmd_disable_cfg[] = {
    0xFD, 0xFC, 0xFB, 0xFA,
    0x02, 0x00,
    0xFE, 0x00,
    0x04, 0x03, 0x02, 0x01
  };
  radarSerial->write(cmd_disable_cfg, sizeof(cmd_disable_cfg));
  delay(50);

  Serial.println("[RADAR] Engineering mode enabled (gate energies active)");
}

#endif /* RADAR_PARSER_H_ */
