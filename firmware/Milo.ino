// ================================================================
// MILO — Unified Firmware v1.5 (HTTP POST ingestion, no MQTT)
// Rotating OLED Display: Mood | Environment | Art
// ================================================================

#define EIDSP_QUANTIZE_FILTERBANK 0

#include <Milo-Audio-Classifier_inferencing.h>
#include <Wire.h>
#include <DHT.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "driver/i2s.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <time.h>

const char* ssid        = "Username"; // Exact name of Hotspot
const char* password    = "Password"; // Hotspot password 

// ── Backend ingestion endpoint (replaces MQTT entirely) ──────────
// Your Flask backend's ngrok static domain, hit with a plain short
// HTTPS POST once per second — no persistent connection to babysit.
const char* ingest_url  = "https://naimot.pythonanywhere.com/api/ingest";
const char* api_key     = "milo-ingest-key-2026"; // must match backend INGEST_API_KEY

// ── NTP time ─────────────────────────────────────────────────────
const char* ntpServer   = "pool.ntp.org";
const long  gmtOffset   = 7200;   // UTC+2 for Finland summer (EEST)
const int   dstOffset   = 0;

// ── Pins ─────────────────────────────────────────────────────────
#define DHT_PIN        4
#define MQ135_AO       5
#define MQ135_DO       6
#define PIR_PIN        15
#define I2S_WS         42
#define I2S_SCK        41
#define I2S_SD         2
#define SCREEN_WIDTH   128
#define SCREEN_HEIGHT  64
#define OLED_RESET     -1
#define SCREEN_ADDRESS 0x3C

// ── Thresholds ───────────────────────────────────────────────────
#define TEMP_WARM         30.0
#define TEMP_STUFFY       33.0
#define HUMIDITY_STUFFY   70.0
#define MQ135_STUFFY      2800
#define AUDIO_KEYBOARD    0.55
#define AUDIO_NOISE       0.50
#define MOTION_TIMEOUT    120000

// ── Display rotation ─────────────────────────────────────────────
#define SCREEN_COUNT      4       // mood, env, artA, artB
#define SCREEN_INTERVAL   5000   // 5 seconds per screen
int currentScreen = 0;
unsigned long lastScreenSwitch = 0;

// ── Art animation state ──────────────────────────────────────────
int animFrame = 0;
unsigned long lastAnimFrame = 0;
#define ANIM_INTERVAL 400

// ── Face hold timer ───────────────────────────────────────────────
#define FACE_HOLD_MS      3000
String lastFace    = "content";
unsigned long faceHoldTime = 0;

// ── PIR timeout ───────────────────────────────────────────────────
unsigned long lastMotionTime = 0;

// ── HTTP POST interval ──────────────────────────────────────────
#define POST_INTERVAL 1000
unsigned long lastPostTime = 0;

// ── Objects ──────────────────────────────────────────────────────
DHT dht(DHT_PIN, DHT22);
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// Note: no persistent TLS session to manage anymore — HTTPClient
// handles the HTTPS handshake internally per-request, and we use
// setInsecure() since these are short, one-shot POSTs rather than
// a long-lived connection (the failure mode we were fighting with
// MQTT doesn't apply the same way here).

// ── Sensor globals ────────────────────────────────────────────────
float g_temp = 0, g_humidity = 0;
int   g_mq135 = 0;
bool  g_motion = false;
String g_audio = "silence";

// ── Audio inference ───────────────────────────────────────────────
typedef struct {
  int16_t *buffer;
  uint8_t  buf_ready;
  uint32_t buf_count;
  uint32_t n_samples;
} inference_t;
static inference_t inference;
static const uint32_t sample_buffer_size = 2048;
static signed short sampleBuffer[sample_buffer_size];
static bool record_status = true;

// ================================================================
// WIFI + MQTT
// ================================================================
void connectWiFi() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(28, 20);
  display.print("Connecting WiFi");
  display.setCursor(48, 36);
  display.print(". . .");
  display.display();

  WiFi.begin(ssid, password);
  int dots = 0;
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    dots++;
    display.clearDisplay();
    display.setCursor(28, 20);
    display.print("Connecting WiFi");
    display.setCursor(48, 36);
    for (int i = 0; i < (dots % 4); i++) display.print(". ");
    display.display();
  }

  configTime(gmtOffset, dstOffset, ntpServer);

  display.clearDisplay();
  display.setCursor(20, 20);
  display.print("WiFi connected!");
  display.setCursor(8, 36);
  display.print(WiFi.localIP());
  display.display();
  delay(2000);
}

void publishData(float temp, float humidity, int mq135,
                 bool motion, String face, String audio) {

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi down — skipping post this cycle");
    return;
  }

  // Rate limit posting — no connection state to manage, so this is
  // the only gate we need.
  if (millis() - lastPostTime < POST_INTERVAL) return;
  lastPostTime = millis();

  char payload[192];
  snprintf(payload, sizeof(payload),
    "{\"temp\":%.1f,\"humidity\":%.1f,\"air\":%d,\"motion\":%s,\"face\":\"%s\",\"audio\":\"%s\"}",
    temp, humidity, mq135,
    motion ? "true" : "false",
    face.c_str(), audio.c_str());

  WiFiClientSecure client;
  client.setInsecure(); // short one-shot request, not a long-lived session
  HTTPClient https;

  if (https.begin(client, ingest_url)) {
    https.addHeader("Content-Type", "application/json");
    https.addHeader("X-API-Key", api_key);
    https.setTimeout(5000);

    int httpCode = https.POST((uint8_t*)payload, strlen(payload));

    Serial.print("POST: ");
    if (httpCode > 0) {
      Serial.print(httpCode);
      Serial.print(" | freeHeap="); Serial.println(ESP.getFreeHeap());
    } else {
      Serial.print("FAILED (");
      Serial.print(https.errorToString(httpCode));
      Serial.print(") | freeHeap="); Serial.println(ESP.getFreeHeap());
    }
    https.end();
  } else {
    Serial.println("POST: could not begin HTTPS connection");
  }
}

// ================================================================
// SCREEN 1 — MOOD FACE (FIXED Y-CLEARANCE & BLINK ANIMATION)
// ================================================================
// ================================================================
// MILO EXPRESSION SYSTEM
// ================================================================
// Design language: big expressive eyes carry most of the emotion
// (~65% of the face), the mouth is small and secondary (~25%), and
// small accent details (eyebrows, sweat, zzz) do the rest (~10%).
// Every state shares the same blink + look-around animation so the
// whole face feels alive, not just one hardcoded state.

// 0.0 = fully closed, 1.0 = fully open. Blinks for 2 frames out of
// every 20-frame animFrame cycle (driven by ANIM_INTERVAL already
// ticking in loop()), with a quick half-open/half-close either side
// so the blink doesn't look like a hard cut.
float eyeOpenness() {
  if (animFrame >= 18) return 0.0;   // closed
  if (animFrame == 17) return 0.4;   // closing
  if (animFrame == 0)  return 0.4;   // opening
  return 1.0;                         // open
}

// Subtle left/right pupil drift — changes every ~7 frames, kept small
// (+/-2px) so it reads as "alive" rather than distracting.
int lookOffsetX() {
  int phase = (animFrame / 7) % 3;
  if (phase == 1) return -2;
  if (phase == 2) return 2;
  return 0;
}

// Draws one eye at (cx, cy). `openMul` scales the base openness — use
// <1.0 for permanently heavy-lidded looks (sleepy, stuffy) and >1.0
// isn't meaningful (clamped). `pupil=false` for states that just want
// a plain circle with no pupil dot (e.g. wide-alert eyes read fine
// with just a small centered pupil, so this is rarely needed).
void drawEye(int cx, int cy, int r, float openMul = 1.0, bool pupil = true) {
  float open = eyeOpenness() * openMul;
  if (open > 1.0) open = 1.0;
  int lookX = lookOffsetX();

  if (open <= 0.08) {
    display.drawLine(cx - r, cy, cx + r, cy, SSD1306_WHITE);
    display.drawLine(cx - r, cy + 1, cx + r, cy + 1, SSD1306_WHITE);
    return;
  }

  display.fillCircle(cx, cy, r, SSD1306_WHITE);
  if (open < 0.95) {
    int eyeH  = max(2, (int)(r * 2 * open));
    int maskH = (r * 2 - eyeH) / 2;
    display.fillRect(cx - r - 1, cy - r - 1, r * 2 + 2, maskH, SSD1306_BLACK);
    display.fillRect(cx - r - 1, cy + r - maskH + 1, r * 2 + 2, maskH, SSD1306_BLACK);
  }
  if (pupil && open > 0.3) {
    display.fillCircle(cx + lookX, cy, max(2, r / 2), SSD1306_BLACK);
  }
}

// Draws a smile/frown mouth using the "circle + mask" trick: draw a
// full circle outline, then black out either the top half (leaving a
// smile-shaped bottom arc) or the bottom half (leaving a frown-shaped
// top arc).
void drawMouthArc(int cx, int cy, int r, bool smile) {
  display.drawCircle(cx, cy, r, SSD1306_WHITE);
  if (smile) {
    display.fillRect(cx - r - 1, cy - r - 1, r * 2 + 2, r + 1, SSD1306_BLACK);
  } else {
    display.fillRect(cx - r - 1, cy, r * 2 + 2, r + 1, SSD1306_BLACK);
  }
}

const int EYE_L_X = 38, EYE_R_X = 90, EYE_Y = 24, EYE_R = 13;
const int MOUTH_X = 64, MOUTH_Y = 34;

void drawFaceContent() {
  drawEye(EYE_L_X, EYE_Y, EYE_R);
  drawEye(EYE_R_X, EYE_Y, EYE_R);
  drawMouthArc(MOUTH_X, MOUTH_Y, 9, true); // gentle smile
}
void drawFaceWarm() {
  // Raised eyebrows — a hint of discomfort, eyes stay open/normal
  display.drawLine(EYE_L_X - 12, 6, EYE_L_X + 4, 3, SSD1306_WHITE);
  display.drawLine(EYE_R_X - 4, 3, EYE_R_X + 12, 6, SSD1306_WHITE);
  drawEye(EYE_L_X, EYE_Y, EYE_R);
  drawEye(EYE_R_X, EYE_Y, EYE_R);
  // Uncomfortable wavy mouth
  int wy = MOUTH_Y + 6;
  display.drawLine(MOUTH_X - 24, wy,     MOUTH_X - 16, wy - 4, SSD1306_WHITE);
  display.drawLine(MOUTH_X - 16, wy - 4, MOUTH_X - 8,  wy,     SSD1306_WHITE);
  display.drawLine(MOUTH_X - 8,  wy,     MOUTH_X,      wy - 4, SSD1306_WHITE);
  display.drawLine(MOUTH_X,      wy - 4, MOUTH_X + 8,  wy,     SSD1306_WHITE);
  display.drawLine(MOUTH_X + 8,  wy,     MOUTH_X + 16, wy - 4, SSD1306_WHITE);
  // Small heat-drop accent, top right
  display.fillCircle(112, 12, 3, SSD1306_WHITE);
  display.fillTriangle(109, 12, 115, 12, 112, 4, SSD1306_WHITE);
}
void drawFaceStuffy() {
  // Heavy, droopy eyelids — permanently ~30% open rather than the
  // normal blink cycle, so it reads as "struggling to keep eyes open"
  drawEye(EYE_L_X, EYE_Y, EYE_R, 0.35);
  drawEye(EYE_R_X, EYE_Y, EYE_R, 0.35);
  // Small sighing/exhaling mouth — a short flat line with a puff mark
  display.drawLine(MOUTH_X - 10, MOUTH_Y + 8, MOUTH_X + 10, MOUTH_Y + 8, SSD1306_WHITE);
  display.drawCircle(MOUTH_X + 16, MOUTH_Y + 8, 2, SSD1306_WHITE);
  display.drawCircle(MOUTH_X + 21, MOUTH_Y + 8, 1, SSD1306_WHITE);
}
void drawFaceAlert() {
  // Wide, fully-open circular eyes (blink cycle overridden — alert
  // eyes shouldn't get sleepy-looking mid-blink) + small round mouth
  display.fillCircle(EYE_L_X, EYE_Y, EYE_R + 2, SSD1306_WHITE);
  display.fillCircle(EYE_L_X, EYE_Y, (EYE_R + 2) / 2, SSD1306_BLACK);
  display.fillCircle(EYE_R_X, EYE_Y, EYE_R + 2, SSD1306_WHITE);
  display.fillCircle(EYE_R_X, EYE_Y, (EYE_R + 2) / 2, SSD1306_BLACK);
  display.fillCircle(MOUTH_X, MOUTH_Y + 8, 4, SSD1306_WHITE);
  display.fillCircle(MOUTH_X, MOUTH_Y + 8, 2, SSD1306_BLACK);
}
void drawFaceStressed() {
  // Uneven eyebrows — one arched higher than the other, asymmetric
  // and worried-looking rather than the old symmetric slant
  display.drawLine(EYE_L_X - 14, 4,  EYE_L_X - 2, 9, SSD1306_WHITE);
  display.drawLine(EYE_L_X - 14, 5,  EYE_L_X - 2, 10, SSD1306_WHITE);
  display.drawLine(EYE_R_X + 2, 10, EYE_R_X + 14, 12, SSD1306_WHITE);
  drawEye(EYE_L_X, EYE_Y, EYE_R);
  drawEye(EYE_R_X, EYE_Y, EYE_R);
  drawMouthArc(MOUTH_X, MOUTH_Y + 4, 9, false); // worried frown
}
void drawFaceSleepy() {
  // Half-closed eyes constantly (not tied to the blink cycle — sleepy
  // should never look "wide awake" between blinks)
  display.fillCircle(EYE_L_X, EYE_Y, EYE_R, SSD1306_WHITE);
  display.fillRect(EYE_L_X - EYE_R - 1, EYE_Y - EYE_R - 1, EYE_R * 2 + 2, EYE_R, SSD1306_BLACK);
  display.fillCircle(EYE_R_X, EYE_Y, EYE_R, SSD1306_WHITE);
  display.fillRect(EYE_R_X - EYE_R - 1, EYE_Y - EYE_R - 1, EYE_R * 2 + 2, EYE_R, SSD1306_BLACK);
  // Tiny flat smile
  display.drawLine(MOUTH_X - 10, MOUTH_Y + 8, MOUTH_X + 10, MOUTH_Y + 8, SSD1306_WHITE);
  display.setTextSize(2);
  display.setCursor(100, 4);
  display.print("z");
  display.setTextSize(1);
  display.setCursor(114, 0);
  display.print("z");
}

void drawMoodScreen(String face, float temp) {
  display.clearDisplay();
  if      (face == "warm")     drawFaceWarm();
  else if (face == "stuffy")   drawFaceStuffy();
  else if (face == "alert")    drawFaceAlert();
  else if (face == "stressed") drawFaceStressed();
  else if (face == "sleepy")   drawFaceSleepy();
  else                         drawFaceContent();

  display.drawLine(0, 52, 127, 52, SSD1306_WHITE);

  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(2, 56);
  display.print(face);

  struct tm timeinfo;
  if (getLocalTime(&timeinfo)) {
    char timeStr[6];
    strftime(timeStr, sizeof(timeStr), "%H:%M", &timeinfo);
    display.setCursor(92, 56);
    display.print(timeStr);
  } else {
    display.setCursor(86, 56);
    display.print(temp, 1);
    display.print("C");
  }

  display.display();
}

// ================================================================
// SCREEN 2 — ENVIRONMENT DATA 
// ================================================================
String airLabel(int air) {
  if (air < 1500) return "EXCEL.";
  if (air < 2000) return "GOOD";
  if (air < 2500) return "FAIR";
  return "POOR";
}

void drawEnvScreen(float temp, float humidity, int mq135, int wellness) {
  display.clearDisplay();

  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(30, 2);
  display.print("ENVIRONMENT");

  // Row 1: Icons & Labels
  display.drawRect(8, 16, 2, 7, SSD1306_WHITE);
  display.fillCircle(8, 24, 2, SSD1306_WHITE);
  display.setCursor(13, 17);
  display.print("Temp:");

  display.fillTriangle(58, 15, 56, 19, 60, 19, SSD1306_WHITE);
  display.fillCircle(58, 20, 2, SSD1306_WHITE);
  display.setCursor(63, 17);
  display.print("Hum:");

  display.drawCircle(105, 18, 3, SSD1306_WHITE);
  display.drawLine(105, 21, 102, 24, SSD1306_WHITE);
  display.setCursor(110, 17);
  display.print("Air:");

  // Row 2: Values
  display.setCursor(4, 28);
  display.print(temp, 1);
  display.print("C");

  display.setCursor(56, 28);
  display.print(humidity, 0);
  display.print("%");

  display.setCursor(92, 28);
  display.print(airLabel(mq135));

  // Row 3: Wellness Text & Radial Arc Gauge
  display.setCursor(4, 42);
  display.print("Wellness");

  int cx = 106, cy = 52, r = 11;
  for (int i = 0; i <= 180; i += 15) {
    float rad = i * 3.14159 / 180.0;
    int x = cx - (int)(r * cos(rad));
    int y = cy - (int)(r * sin(rad));
    display.drawPixel(x, y, SSD1306_WHITE);
  }
  float needleRad = (180 - map(wellness, 0, 100, 0, 180)) * 3.14159 / 180.0;
  int nx = cx - (int)((r - 2) * cos(needleRad));
  int ny = cy - (int)((r - 2) * sin(needleRad));
  display.drawLine(cx, cy, nx, ny, SSD1306_WHITE);

  int segments = map(wellness, 0, 100, 0, 12);
  for (int i = 0; i < 12; i++) {
    int x = 4 + (i * 6);
    if (i < segments) {
      display.fillRect(x, 52, 4, 8, SSD1306_WHITE);
    } else {
      display.drawRect(x, 52, 4, 8, SSD1306_WHITE);
    }
  }

  display.setCursor(80, 52);
  display.print(wellness);
  display.print("%");

  display.display();
}
// ================================================================
// SCREEN 3A — AMBIENT TIME ART
// ================================================================
void drawAmbientArt() {
  display.clearDisplay();

  struct tm timeinfo;
  int hour = 12;
  if (getLocalTime(&timeinfo)) hour = timeinfo.tm_hour;

  if (hour >= 5 && hour < 12) {
    int sunY = 40 - (animFrame * 3);
    if (sunY < 20) sunY = 20;
    display.fillCircle(64, sunY, 12, SSD1306_WHITE);
    for (int i = 0; i < 8; i++) {
      float angle = i * 3.14159 / 4;
      int x1 = 64 + 16 * cos(angle);
      int y1 = sunY + 16 * sin(angle);
      int x2 = 64 + 22 * cos(angle);
      int y2 = sunY + 22 * sin(angle);
      display.drawLine(x1, y1, x2, y2, SSD1306_WHITE);
    }
    display.drawLine(0, 50, 127, 50, SSD1306_WHITE);
    display.setCursor(36, 54);
    display.setTextSize(1);
    display.print("Good morning");

  } else if (hour >= 12 && hour < 18) {
    for (int i = 0; i < 5; i++) {
      int y = 10 + i * 12;
      int offset = (animFrame % 2 == 0) ? 0 : 4;
      display.drawLine(offset, y, 127 - offset, y, SSD1306_WHITE);
    }
    display.drawLine(0, 48, 127, 48, SSD1306_WHITE);
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(22, 54);
    display.print("Good afternoon");
    
  } else if (hour >= 18 && hour < 22) {
    int starCount = min(animFrame + 3, 12);
    int stars[][2] = {{10,8},{30,15},{55,5},{80,12},{100,8},{20,30},
                      {45,25},{70,20},{95,28},{15,45},{60,40},{110,35}};
    for (int i = 0; i < starCount; i++) {
      display.fillCircle(stars[i][0], stars[i][1], 1, SSD1306_WHITE);
      display.drawPixel(stars[i][0]-2, stars[i][1], SSD1306_WHITE);
      display.drawPixel(stars[i][0]+2, stars[i][1], SSD1306_WHITE);
      display.drawPixel(stars[i][0], stars[i][1]-2, SSD1306_WHITE);
      display.drawPixel(stars[i][0], stars[i][1]+2, SSD1306_WHITE);
    }
    display.fillCircle(100, 42, 10, SSD1306_WHITE);
    display.fillCircle(106, 38, 9, SSD1306_BLACK);
    display.setCursor(36, 54);
    display.setTextSize(1);
    display.print("Good evening");

  } else {
    int stars[][2] = {{10,8},{30,15},{55,5},{80,12},{20,30},
                      {45,25},{70,20},{15,45},{60,40}};
    for (auto& s : stars) {
      display.fillCircle(s[0], s[1], 1, SSD1306_WHITE);
    }
    display.fillCircle(100, 30, 12, SSD1306_WHITE);
    display.fillCircle(107, 25, 10, SSD1306_BLACK);
    display.setCursor(42, 54);
    display.setTextSize(1);
    display.print("Good night");
  }

  display.display();
}

// ================================================================
// SCREEN 3B — MILO'S WORLD (ANIMATED PIXEL ART & FIXED TEXT)
// ================================================================
void drawMiloWorld() {
  display.clearDisplay();

  // Moving Sky Clouds
  int cloudX1 = (animFrame * 4) % 140 - 20;
  int cloudX2 = ((animFrame * 3) + 60) % 140 - 20;
  
  display.fillCircle(cloudX1, 8, 4, SSD1306_WHITE);
  display.fillCircle(cloudX1 + 5, 6, 5, SSD1306_WHITE);
  display.fillCircle(cloudX1 + 10, 8, 4, SSD1306_WHITE);
  
  display.fillCircle(cloudX2, 12, 3, SSD1306_WHITE);
  display.fillCircle(cloudX2 + 4, 11, 4, SSD1306_WHITE);

  // Window on Wall
  display.drawRect(4, 4, 20, 24, SSD1306_WHITE);
  display.drawLine(14, 4, 14, 28, SSD1306_WHITE);
  display.drawLine(4, 16, 24, 16, SSD1306_WHITE);

  // Desk (Lifted to Y=40 for clearance)
  display.fillRect(35, 40, 60, 3, SSD1306_WHITE);
  display.fillRect(38, 43, 3, 9, SSD1306_WHITE);   
  display.fillRect(89, 43, 3, 9, SSD1306_WHITE);   

  // Bouncing Milo
  int bounceY = (animFrame % 2 == 0) ? 0 : -2; 
  int miloY = 32 + bounceY;
  
  display.fillCircle(65, miloY, 7, SSD1306_WHITE);
  display.fillCircle(63, miloY - 1, 1, SSD1306_BLACK);    
  display.fillCircle(67, miloY - 1, 1, SSD1306_BLACK);    
  display.drawLine(63, miloY + 3, 67, miloY + 3, SSD1306_BLACK); 

  // Desk Plant
  display.fillRect(104, 42, 8, 6, SSD1306_WHITE);      
  display.drawLine(108, 42, 108, 32, SSD1306_WHITE);   
  display.fillCircle(105, 30, 4, SSD1306_WHITE);        
  display.fillCircle(111, 30, 4, SSD1306_WHITE);        

  // Floor Line (Clean at Y=52)
  display.drawLine(0, 52, 127, 52, SSD1306_WHITE);

  // Bottom Label (Placed neatly at Y=56)
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(28, 56);
  display.print("~ Milo's World ~");

  display.display();
}

// ================================================================
// FACE DECISION ENGINE
// ================================================================
String decideFace(float temp, float humidity, int mq135,
                  bool motion, float conf_noise, float conf_keyboard) {
  if (conf_keyboard >= AUDIO_KEYBOARD) return "stressed";
  if (conf_noise    >= AUDIO_NOISE)    return "alert";
  if (temp > TEMP_STUFFY || humidity > HUMIDITY_STUFFY
      || mq135 > MQ135_STUFFY)         return "stuffy";
  if (temp > TEMP_WARM)                return "warm";
  if (!motion)                         return "sleepy";
  return "content";
}

int calcWellness(float temp, float hum, int air, String audio) {
  int t = 100;
  if (temp > 33) t = 20; else if (temp > 30) t = 40;
  else if (temp > 28) t = 60; else if (temp > 25) t = 80;
  int h = 100;
  if (hum < 30 || hum > 70) h = 40;
  else if (hum < 40 || hum > 60) h = 85;
  int a = 100;
  if (air > 3200) a = 20; else if (air > 2800) a = 40;
  else if (air > 2400) a = 60; else if (air > 2000) a = 80;
  int n = 100;
  if (audio == "noise") n = 50;
  else if (audio == "keyboard") n = 75;
  return (t*30 + h*20 + a*25 + n*15 + 95*10) / 100;
}

// ================================================================
// I2S AUDIO
// ================================================================
static void audio_inference_callback(uint32_t n_bytes) {
  for (int i = 0; i < n_bytes >> 1; i++) {
    inference.buffer[inference.buf_count++] = sampleBuffer[i];
    if (inference.buf_count >= inference.n_samples) {
      inference.buf_count = 0;
      inference.buf_ready = 1;
    }
  }
}
static void capture_samples(void* arg) {
  const int32_t i2s_bytes_to_read = (uint32_t)arg;
  size_t bytes_read = i2s_bytes_to_read;
  while (record_status) {
    i2s_read((i2s_port_t)1, (void*)sampleBuffer,
             i2s_bytes_to_read, &bytes_read, 100);
    if (bytes_read > 0) {
      for (int x = 0; x < i2s_bytes_to_read/2; x++)
        sampleBuffer[x] = (int16_t)(sampleBuffer[x]) * 8;
      if (record_status) audio_inference_callback(i2s_bytes_to_read);
    }
  }
  vTaskDelete(NULL);
}
static bool microphone_inference_start(uint32_t n_samples) {
  inference.buffer = (int16_t*)malloc(n_samples * sizeof(int16_t));
  if (!inference.buffer) return false;
  inference.buf_count = 0;
  inference.n_samples = n_samples;
  inference.buf_ready = 0;
  i2s_config_t i2s_config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER|I2S_MODE_RX|I2S_MODE_TX),
    .sample_rate = EI_CLASSIFIER_FREQUENCY,
    .bits_per_sample = (i2s_bits_per_sample_t)16,
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = I2S_COMM_FORMAT_I2S,
    .intr_alloc_flags = 0,
    .dma_buf_count = 8,
    .dma_buf_len = 512,
    .use_apll = false,
    .tx_desc_auto_clear = false,
    .fixed_mclk = -1,
  };
  i2s_pin_config_t pin_config = {
    .bck_io_num = I2S_SCK,
    .ws_io_num = I2S_WS,
    .data_out_num = -1,
    .data_in_num = I2S_SD
  };
  i2s_driver_install((i2s_port_t)1, &i2s_config, 0, NULL);
  i2s_set_pin((i2s_port_t)1, &pin_config);
  i2s_zero_dma_buffer((i2s_port_t)1);
  ei_sleep(100);
  record_status = true;
  xTaskCreatePinnedToCore(capture_samples, "CaptureSamples", 1024*32,
              (void*)sample_buffer_size, 10, NULL, 1);
  // Pinned to Core 1 (same as loop/Arduino task) so Core 0 — which runs
  // the WiFi/LWIP/TLS stack — never gets starved by audio capture.
  // This was very likely the cause of the ~2-4s MQTT drops: an
  // unpinned task could land on Core 0 and block the network stack
  // from servicing the TLS socket in time.
  return true;
}
static bool microphone_inference_record(void) {
  while (inference.buf_ready == 0) {
    delay(10);
  }
  inference.buf_ready = 0;
  return true;
}
static int microphone_audio_signal_get_data(size_t offset,
    size_t length, float* out_ptr) {
  numpy::int16_to_float(&inference.buffer[offset], out_ptr, length);
  return 0;
}

// ================================================================
// SETUP
// ================================================================
void setup() {
  Serial.begin(115200);
  delay(1000);

  dht.begin();
  pinMode(PIR_PIN, INPUT);
  pinMode(MQ135_DO, INPUT);
  Wire.begin(8, 9);

  if (!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    Serial.println("OLED not found!");
    while (true);
  }

  // Boot screen
  display.clearDisplay();
  display.setTextSize(2);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(28, 16);
  display.print("MILO");
  display.setTextSize(1);
  display.setCursor(40, 38);
  display.print("v 1.5");
  display.setCursor(18, 52);
  display.print("smart companion");
  display.display();
  delay(2000);
  
  connectWiFi();

  if (!microphone_inference_start(EI_CLASSIFIER_RAW_SAMPLE_COUNT)) {
    Serial.println("ERR: Audio buffer failed!");
    while (true);
  }

  
  faceHoldTime = millis();
  lastScreenSwitch = millis();
  Serial.println("Milo v1.5 ready!");
}

// ================================================================
// LOOP
// ================================================================
void loop() {
  // 1. Read sensors
  float temp     = dht.readTemperature();
  float humidity = dht.readHumidity();
  int   mq135    = analogRead(MQ135_AO);

  bool rawMotion = digitalRead(PIR_PIN);
  if (rawMotion) lastMotionTime = millis();
  bool motion = (millis() - lastMotionTime) < MOTION_TIMEOUT;

  if (isnan(temp) || isnan(humidity)) {
    delay(2000);
    return;
  }

  g_temp = temp; g_humidity = humidity;
  g_mq135 = mq135; g_motion = motion;

  // 2. Audio inference
  microphone_inference_record();
  signal_t signal;
  signal.total_length = EI_CLASSIFIER_RAW_SAMPLE_COUNT;
  signal.get_data = &microphone_audio_signal_get_data;
  ei_impulse_result_t result = { 0 };
  run_classifier(&signal, &result, false);

  float conf_keyboard = 0, conf_noise = 0, conf_silence = 0;
  String topAudio = "silence"; float topConf = 0;
  for (size_t ix = 0; ix < EI_CLASSIFIER_LABEL_COUNT; ix++) {
    String label = result.classification[ix].label;
    float  val   = result.classification[ix].value;
    if (label == "keyboard") conf_keyboard = val;
    else if (label == "noise")   conf_noise   = val;
    else if (label == "silence") conf_silence = val;
    if (val > topConf) { topConf = val; topAudio = label; }
  }
  g_audio = topAudio;

  // 3. Face decision
  String newFace = decideFace(temp, humidity, mq135, motion,
                               conf_noise, conf_keyboard);
  if (newFace != lastFace) {
    if (millis() - faceHoldTime >= FACE_HOLD_MS) {
      lastFace = newFace;
      faceHoldTime = millis();
    }
  } else {
    faceHoldTime = millis();
  }

  // 4. Screen rotation
  if (millis() - lastScreenSwitch >= SCREEN_INTERVAL) {
    currentScreen = (currentScreen + 1) % SCREEN_COUNT;
    lastScreenSwitch = millis();
  }

  // Animation frame
  if (millis() - lastAnimFrame >= ANIM_INTERVAL) {
    animFrame++;
    if (animFrame > 20) animFrame = 0;
    lastAnimFrame = millis();
  }

  // 5. Draw current screen
  int wellness = calcWellness(temp, humidity, mq135, topAudio);
  switch (currentScreen) {
    case 0: drawMoodScreen(lastFace, temp);                 break;
    case 1: drawEnvScreen(temp, humidity, mq135, wellness);   break;
    case 2: drawAmbientArt();                                break;
    case 3: drawMiloWorld();                                 break;
  }

  // 6. MQTT publish (JUST THE FUNCTION CALL)
  publishData(temp, humidity, mq135, motion, lastFace, topAudio);

  // 7. Serial
  Serial.print(lastFace); Serial.print(" | screen:");
  Serial.print(currentScreen); Serial.print(" | ");
  Serial.print(temp,1); Serial.print("C | ");
  Serial.println(topAudio);

} 

#if !defined(EI_CLASSIFIER_SENSOR) || \
    EI_CLASSIFIER_SENSOR != EI_CLASSIFIER_SENSOR_MICROPHONE
#error "Invalid model for current sensor."
#endif