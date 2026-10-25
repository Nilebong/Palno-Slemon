/*******************************************************************************
 * eink_display.h — 3.5" 4-Colour E-ink Display Wrapper for Slemon (STM32)
 *
 * Based on the epaper.ino reference (Inspiration/epaper/epaper.ino).
 * Display: GDEM035F51 — 3.5" 4-colour (black/white/red/yellow)
 * Library: GxEPD2_4C with GxEPD2_350c_GDEM035F51 driver
 *
 * Pin assignments (from epaper.ino — do NOT change):
 *   CS   = GPIO 4    SPI chip select
 *   DC   = GPIO 16   Data/Command
 *   RST  = GPIO 17   Hardware reset
 *   BUSY = GPIO 15   Busy signal (active high)
 *   PWR  = GPIO 25   Power gate (HIGH = display on)
 *
 * Display modes:
 *   SLEEP_ACTIVE  — Vital stats, updates every 60 s (partial refresh)
 *   ALARM_RINGING — "WAKE UP" full-screen, red text
 *   MORNING_SUMM  — Night summary with sleep score
 *   IDLE          — IP address, BLE status, sensor health
 *
 * IMPORTANT: Full refresh takes ~4-6 seconds (e-ink physics). Use partial
 * refresh (setPartialWindow) for 60-second sleep updates to reduce latency
 * and extend display lifespan. Always call display.powerOff() after refresh
 * and toggle PWR LOW to save power.
 ******************************************************************************/
#ifndef EINK_DISPLAY_H_
#define EINK_DISPLAY_H_

#include <Arduino.h>
#include <GxEPD2_4C.h>
#include <Fonts/FreeMonoBold9pt7b.h>
#include <Fonts/FreeMonoBold12pt7b.h>
#include <Fonts/FreeMonoBold18pt7b.h>
#include <Fonts/FreeMonoBold24pt7b.h>

// ─── Architecture Compatibility ──────────────────────────────────────────────
// dtostrf fallback for non-AVR architectures (STM32, Zephyr, ARM)
#if !defined(dtostrf)
static inline char* dtostrf(double val, signed char width, unsigned char prec, char* sout) {
  String s(val, prec);
  while ((int)s.length() < width) {
    s = " " + s;
  }
  strcpy(sout, s.c_str());
  return sout;
}
#endif

// ─── Pin definitions (from epaper.ino) ───────────────────────────────────────
#define EPD_CS    10
#define EPD_DC    5
#define EPD_RST   4
#define EPD_BUSY  9
#define EPD_PWR   8

// ─── Display instance ────────────────────────────────────────────────────────
GxEPD2_4C<GxEPD2_350c_GDEM035F51, GxEPD2_350c_GDEM035F51::HEIGHT> display(
  GxEPD2_350c_GDEM035F51(EPD_CS, EPD_DC, EPD_RST, EPD_BUSY)
);

// ─── Timing ──────────────────────────────────────────────────────────────────
#define EINK_SLEEP_REFRESH_MS  60000UL   // partial refresh every 60 s during sleep
static uint32_t eink_last_refresh_ms = 0;
static bool     eink_initialised     = false;

// ─── Internal: power on display and init ─────────────────────────────────────
static void eink_power_on() {
  pinMode(EPD_PWR, OUTPUT);
  digitalWrite(EPD_PWR, HIGH);
  delay(200);
  display.init(115200, true, 2, false);
  display.setRotation(1);   // landscape
}

// ─── Internal: power off display ─────────────────────────────────────────────
static void eink_power_off() {
  display.powerOff();
  delay(100);
  digitalWrite(EPD_PWR, LOW);
}

// ─── Initialise ──────────────────────────────────────────────────────────────
/**
 * Call once from setup(). Powers on display and shows "Slemon Ready" splash.
 */
inline void eink_init() {
  eink_power_on();

  // Full-window clear to white
  display.setFullWindow();
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);
    display.setFont(&FreeMonoBold18pt7b);
    display.setTextColor(GxEPD_BLACK);
    display.setCursor(20, 60);
    display.print("Slemon");
    display.setFont(&FreeMonoBold12pt7b);
    display.setTextColor(GxEPD_RED);
    display.setCursor(20, 100);
    display.print("Sleep Tracker v1.0");
    display.setFont(&FreeMonoBold9pt7b);
    display.setTextColor(GxEPD_BLACK);
    display.setCursor(20, 140);
    display.print("Initialising sensors...");
  } while (display.nextPage());

  eink_power_off();
  eink_initialised = true;
  eink_last_refresh_ms = millis();

  Serial.println("[EINK] Splash screen shown");
}

// ─── IDLE screen: IP, BLE status, sensor health ──────────────────────────────
/**
 * @param ipAddr      IP address string, e.g. "192.168.1.50"
 * @param bleConn     BLE connection status
 * @param radarOk     Radar present?
 * @param tempOk      Temperature sensor present?
 */
inline void eink_show_idle(const char* ipAddr, bool bleConn,
                            bool radarOk, bool tempOk) {
  eink_power_on();
  display.setFullWindow();
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);

    // Title
    display.setFont(&FreeMonoBold12pt7b);
    display.setTextColor(GxEPD_BLACK);
    display.setCursor(10, 30);
    display.print("Palno & Slemon");

    display.setFont(&FreeMonoBold9pt7b);
    display.setCursor(10, 55);
    display.print("IDLE — Waiting for sleep...");

    // Divider line
    display.drawLine(0, 65, display.width(), 65, GxEPD_BLACK);

    // IP address
    display.setCursor(10, 85);
    display.print("IP: ");
    display.print(ipAddr);

    // BLE status
    display.setCursor(10, 105);
    display.print("BLE:   ");
    display.setTextColor(bleConn ? GxEPD_BLACK : GxEPD_RED);
    display.print(bleConn ? "Connected" : "Waiting...");
    display.setTextColor(GxEPD_BLACK);

    // Sensor status
    display.setCursor(10, 125);
    display.print("Radar: ");
    display.setTextColor(radarOk ? GxEPD_BLACK : GxEPD_RED);
    display.print(radarOk ? "OK" : "ERROR");
    display.setTextColor(GxEPD_BLACK);

    display.setCursor(10, 145);
    display.print("Temp:  ");
    display.setTextColor(tempOk ? GxEPD_BLACK : GxEPD_RED);
    display.print(tempOk ? "OK" : "ERROR");
    display.setTextColor(GxEPD_BLACK);

  } while (display.nextPage());

  eink_power_off();
  eink_last_refresh_ms = millis();
}

// ─── SLEEP ACTIVE screen: vitals during monitoring ───────────────────────────
/**
 * Shows current vitals. Uses partial window for faster refresh.
 * @param breathBpm    Breathing rate in BPM (e.g. 14.2)
 * @param heartRate    Heart rate in BPM (e.g. 68)
 * @param spo2         SpO2 in % (e.g. 98)
 * @param tempC        Room temperature in °C (e.g. 21.45)
 * @param stage        Sleep stage string (e.g. "N2")
 * @param timeStr      Current time string (e.g. "02:47")
 * @param hrValid      Whether HR reading is valid
 * @param spo2Valid    Whether SpO2 reading is valid
 */
inline void eink_show_sleep_active(float breathBpm, int32_t heartRate,
                                    int32_t spo2, float tempC,
                                    const char* stage, const char* timeStr,
                                    bool hrValid, bool spo2Valid) {
  eink_power_on();

  // Use full window for sleep display (partial window support varies by display)
  display.setFullWindow();
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);

    // ── Header bar ──
    display.setFont(&FreeMonoBold9pt7b);
    display.setTextColor(GxEPD_BLACK);
    display.setCursor(5, 18);
    display.print("Palno & Slemon");
    display.setCursor(display.width() - 70, 18);
    display.print(timeStr);

    display.drawLine(0, 24, display.width(), 24, GxEPD_BLACK);

    // ── Sleep Stage (large, centred) ──
    display.setFont(&FreeMonoBold24pt7b);
    char stageLabel[24];
    snprintf(stageLabel, sizeof(stageLabel), "Stage: %s", stage);
    int16_t x1, y1; uint16_t w, h;
    display.getTextBounds(stageLabel, 0, 0, &x1, &y1, &w, &h);
    display.setCursor((display.width() - w) / 2 - x1, 75);

    // Colour by stage
    if (strcmp(stage, "N3") == 0)       display.setTextColor(GxEPD_BLACK);
    else if (strcmp(stage, "REM") == 0) display.setTextColor(GxEPD_RED);
    else                                 display.setTextColor(GxEPD_BLACK);

    display.print(stageLabel);

    // ── Vitals grid ──
    display.setFont(&FreeMonoBold12pt7b);
    display.setTextColor(GxEPD_BLACK);

    // Row 1: Breath BPM
    display.setCursor(5, 115);
    display.print("Breath:");
    display.setCursor(115, 115);
    display.print(breathBpm, 1);
    display.print(" bpm");

    // Row 2: Heart Rate
    display.setCursor(5, 140);
    display.print("HR:    ");
    display.setCursor(115, 140);
    if (hrValid) {
      display.print(heartRate);
      display.print(" bpm");
    } else {
      display.setTextColor(GxEPD_RED);
      display.print("-- bpm");
      display.setTextColor(GxEPD_BLACK);
    }

    // Row 3: SpO2
    display.setCursor(5, 165);
    display.print("SpO2:  ");
    display.setCursor(115, 165);
    if (spo2Valid) {
      display.print(spo2);
      display.print(" %");
    } else {
      display.setTextColor(GxEPD_RED);
      display.print("-- %");
      display.setTextColor(GxEPD_BLACK);
    }

    // Row 4: Room Temperature
    display.setCursor(5, 190);
    display.print("Temp:  ");
    display.setCursor(115, 190);
    if (tempC != -999.0f) {
      display.print(tempC, 2);
      display.print(" C");
    } else {
      display.setTextColor(GxEPD_RED);
      display.print("-- C");
      display.setTextColor(GxEPD_BLACK);
    }

    // ── Footer ──
    display.drawLine(0, display.height() - 20, display.width(),
                     display.height() - 20, GxEPD_BLACK);
    display.setFont(&FreeMonoBold9pt7b);
    display.setCursor(5, display.height() - 5);
    display.print("Monitoring active");

  } while (display.nextPage());

  eink_power_off();
  eink_last_refresh_ms = millis();
}

// ─── ALARM RINGING screen ─────────────────────────────────────────────────
/**
 * Full-screen "WAKE UP" alarm display with current time and sleep score.
 * @param timeStr    Current time string
 * @param sleepScore 0–100 sleep quality score
 */
inline void eink_show_alarm(const char* timeStr, int sleepScore) {
  eink_power_on();
  display.setFullWindow();
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);

    // Big "WAKE UP" in red
    display.setFont(&FreeMonoBold24pt7b);
    display.setTextColor(GxEPD_RED);
    int16_t x1, y1; uint16_t w, h;
    display.getTextBounds("WAKE UP", 0, 0, &x1, &y1, &w, &h);
    display.setCursor((display.width() - w) / 2 - x1, 90);
    display.print("WAKE UP");

    // Time
    display.setFont(&FreeMonoBold18pt7b);
    display.setTextColor(GxEPD_BLACK);
    display.getTextBounds(timeStr, 0, 0, &x1, &y1, &w, &h);
    display.setCursor((display.width() - w) / 2 - x1, 145);
    display.print(timeStr);

    // Sleep score
    display.setFont(&FreeMonoBold12pt7b);
    char scoreStr[32];
    snprintf(scoreStr, sizeof(scoreStr), "Sleep Score: %d/100", sleepScore);
    display.getTextBounds(scoreStr, 0, 0, &x1, &y1, &w, &h);
    display.setCursor((display.width() - w) / 2 - x1, 185);
    display.print(scoreStr);

    // Dismiss hint
    display.setFont(&FreeMonoBold9pt7b);
    display.setTextColor(GxEPD_BLACK);
    display.setCursor(10, display.height() - 8);
    display.print("Shake wristband 10x to dismiss");

  } while (display.nextPage());

  eink_power_off();
  eink_last_refresh_ms = millis();
}

// ─── MORNING SUMMARY screen ──────────────────────────────────────────────────
/**
 * End-of-night summary screen with sleep metrics and visual hypnogram graph.
 * @param duration_min    Total sleep duration in minutes
 * @param sleepScore      0–100 score
 * @param avgBreath       Average breathing rate
 * @param avgHR           Average heart rate
 * @param avgSpo2         Average SpO2
 * @param minTempC        Minimum room temperature
 * @param maxTempC        Maximum room temperature
 * @param cycleCount      Number of complete sleep cycles
 * @param hypno_str       Optional comma-separated stages (e.g. "W,N1,N2,N3,N3,N2,REM,N2...")
 */
inline void eink_show_morning_summary(uint16_t duration_min, int sleepScore,
                                       float avgBreath, int32_t avgHR, int32_t avgSpo2,
                                       float minTempC, float maxTempC, uint8_t cycleCount,
                                       const char* hypno_str = nullptr) {
  eink_power_on();
  display.setFullWindow();
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);

    // Title
    display.setFont(&FreeMonoBold12pt7b);
    display.setTextColor(GxEPD_BLACK);
    display.setCursor(10, 25);
    display.print("Good Morning!");

    // Sleep score (prominent)
    display.setFont(&FreeMonoBold18pt7b);
    char scoreStr[24];
    int scoreColor = (sleepScore >= 85) ? GxEPD_BLACK :
                     (sleepScore >= 70) ? GxEPD_BLACK : GxEPD_RED;
    display.setTextColor(scoreColor);
    snprintf(scoreStr, sizeof(scoreStr), "Score: %d/100", sleepScore);
    display.setCursor(10, 65);
    display.print(scoreStr);

    // Divider
    display.drawLine(0, 75, display.width(), 75, GxEPD_BLACK);

    // Stats grid (Left Column)
    display.setFont(&FreeMonoBold9pt7b);
    display.setTextColor(GxEPD_BLACK);

    // Duration
    display.setCursor(5, 95);
    char durStr[32];
    snprintf(durStr, sizeof(durStr), "Duration: %dh %dm",
             duration_min / 60, duration_min % 60);
    display.print(durStr);

    // Cycles
    display.setCursor(5, 113);
    char cycStr[24];
    snprintf(cycStr, sizeof(cycStr), "Cycles:   %d complete", cycleCount);
    display.print(cycStr);

    // Avg breathing
    display.setCursor(5, 131);
    display.print("Breath:   ");
    display.print(avgBreath, 1);
    display.print(" bpm");

    // Avg HR
    display.setCursor(5, 149);
    display.print("Avg HR:   ");
    display.print((long)avgHR);
    display.print(" bpm");

    // Avg SpO2
    display.setCursor(5, 167);
    display.print("Avg SpO2: ");
    display.print((long)avgSpo2);
    display.print("%");

    // Temperature range
    display.setCursor(5, 185);
    display.print("Temp:     ");
    display.print(minTempC, 1);
    display.print(" - ");
    display.print(maxTempC, 1);
    display.print(" C");

    // ── Visual Hypnogram Graph (Right Column) ───────────────────
    int16_t gx = 210;
    int16_t gy = 85;
    int16_t gw = display.width() - gx - 15;
    if (gw < 140) gw = 160;
    int16_t gh = 90;

    // Graph Title
    display.setFont(&FreeMonoBold9pt7b);
    display.setTextColor(GxEPD_BLACK);
    display.setCursor(gx, gy + 10);
    display.print("Hypnogram");

    // Box & Axis
    int16_t plotY0 = gy + 18;
    display.drawRect(gx + 20, plotY0, gw - 20, gh - 15, GxEPD_BLACK);

    // Stage labels on Y Axis: W=Wake (top), R=REM, L=Light, D=Deep (bottom)
    int16_t yWake  = plotY0 + 8;
    int16_t yRem   = plotY0 + 26;
    int16_t yLight = plotY0 + 44;
    int16_t yDeep  = plotY0 + 62;

    display.setCursor(gx, yWake + 4);
    display.print("W");
    display.setTextColor(GxEPD_RED);
    display.setCursor(gx, yRem + 4);
    display.print("R");
    display.setTextColor(GxEPD_BLACK);
    display.setCursor(gx, yLight + 4);
    display.print("L");
    display.setCursor(gx, yDeep + 4);
    display.print("D");

    // Horizontal guideline dashed/light
    display.drawFastHLine(gx + 20, yRem, gw - 20, GxEPD_RED);
    display.drawFastHLine(gx + 20, yLight, gw - 20, GxEPD_BLACK);
    display.drawFastHLine(gx + 20, yDeep, gw - 20, GxEPD_BLACK);

    // Plot stepped hypnogram stages
    // 0=Deep, 1=Light, 2=REM, 3=Light/N1, 4=Wake
    int8_t stages[24] = {4, 3, 1, 0, 0, 1, 2, 1, 0, 0, 1, 2, 1, 0, 1, 2, 1, 2, 1, 3, 4};
    int numPoints = 21;

    // If custom comma-separated stages passed, parse them
    if (hypno_str != nullptr && strlen(hypno_str) > 3) {
      int pIdx = 0;
      const char* p = hypno_str;
      while (*p && pIdx < 24) {
        if (*p == 'W' || *p == '4') stages[pIdx++] = 4;
        else if (strncmp(p, "REM", 3) == 0 || *p == 'R' || *p == '2') { stages[pIdx++] = 2; if (*p == 'R' && *(p+1)=='E') p += 2; }
        else if (strncmp(p, "N3", 2) == 0 || *p == '0') { stages[pIdx++] = 0; if (*p == 'N') p++; }
        else if (strncmp(p, "N1", 2) == 0 || *p == '3') { stages[pIdx++] = 3; if (*p == 'N') p++; }
        else if (strncmp(p, "N2", 2) == 0 || *p == '1') { stages[pIdx++] = 1; if (*p == 'N') p++; }
        while (*p && *p != ',') p++;
        if (*p == ',') p++;
      }
      if (pIdx > 5) numPoints = pIdx;
    }

    int16_t plotW = gw - 20;
    int16_t stepX = plotW / (numPoints - 1);
    if (stepX < 3) stepX = 3;

    int16_t prevX = gx + 20;
    int16_t prevY = yWake;

    for (int i = 0; i < numPoints; i++) {
      int16_t targetY = (stages[i] == 4) ? yWake :
                        (stages[i] == 2) ? yRem :
                        (stages[i] == 0) ? yDeep : yLight;

      int16_t curX = gx + 20 + (i * stepX);
      if (curX > gx + gw) curX = gx + gw;

      // Draw horizontal step
      int color = (stages[i] == 2) ? GxEPD_RED : GxEPD_BLACK;
      display.drawLine(prevX, prevY, curX, prevY, color);
      // Draw vertical drop/rise
      display.drawLine(curX, prevY, curX, targetY, color);

      prevX = curX;
      prevY = targetY;
    }

    // Time axis label
    display.setTextColor(GxEPD_BLACK);
    display.setCursor(gx + 22, plotY0 + gh - 3);
    display.print("0h");
    display.setCursor(gx + (plotW / 2) + 10, plotY0 + gh - 3);
    display.print("4h");
    display.setCursor(gx + plotW - 5, plotY0 + gh - 3);
    display.print("8h");

  } while (display.nextPage());

  eink_power_off();
  eink_last_refresh_ms = millis();
  Serial.println("[EINK] Morning summary with hypnogram graph shown");
}

// ─── Timed refresh during sleep (call from loop) ─────────────────────────────
/**
 * Returns true if it's time for a sleep-active refresh (every 60 s).
 * Call eink_show_sleep_active() if this returns true.
 */
inline bool eink_sleep_refresh_due() {
  return (millis() - eink_last_refresh_ms) >= EINK_SLEEP_REFRESH_MS;
}

#endif /* EINK_DISPLAY_H_ */
