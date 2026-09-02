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

#include "secrets.h" // WiFi credentials, ingest URL, and API key — see secrets.h.example

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

// ── MQTT interval ─────────────────────────────────────────────────
#define MQTT_INTERVAL 1000
unsigned long lastMqttTime = 0;

// ── Objects ──────────────────────────────────────────────────────
DHT dht(DHT_PIN, DHT22);
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
WiFiClient espClient;
PubSubClient mqtt(espClient);

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

void connectMQTT() {
  while (!mqtt.connected()) {
    if (mqtt.connect(mqtt_client)) {
      mqtt.publish("milo/status", "online");
    } else {
      delay(2000);
    }
  }
}

void publishData(float temp, float humidity, int mq135,
                 bool motion, String face, String audio) {
  if (!mqtt.connected()) connectMQTT();
  mqtt.loop();
  if (millis() - lastMqttTime < MQTT_INTERVAL) return;
  lastMqttTime = millis();
  char buf[16];
  dtostrf(temp, 4, 1, buf);     mqtt.publish("milo/temp", buf);
  dtostrf(humidity, 4, 1, buf); mqtt.publish("milo/humidity", buf);
  itoa(mq135, buf, 10);         mqtt.publish("milo/air", buf);
  mqtt.publish("milo/motion",   motion ? "true" : "false");
  mqtt.publish("milo/face",     face.c_str());
  mqtt.publish("milo/audio",    audio.c_str());
}

// ================================================================
// SCREEN 1 — MOOD FACE
// ================================================================
void drawFaceContent() {
  display.fillCircle(34, 26, 9, SSD1306_WHITE);
  display.fillCircle(34, 27, 5, SSD1306_BLACK);
  display.fillCircle(37, 23, 2, SSD1306_WHITE);
  display.fillCircle(84, 26, 9, SSD1306_WHITE);
  display.fillCircle(84, 27, 5, SSD1306_BLACK);
  display.fillCircle(87, 23, 2, SSD1306_WHITE);
  display.drawCircle(59, 42, 11, SSD1306_WHITE);
  display.fillRect(36, 32, 46, 12, SSD1306_BLACK);
}
void drawFaceWarm() {
  display.drawLine(22, 24, 36, 20, SSD1306_WHITE);
  display.drawLine(22, 25, 36, 21, SSD1306_WHITE);
  display.drawLine(72, 20, 86, 24, SSD1306_WHITE);
  display.drawLine(72, 21, 86, 25, SSD1306_WHITE);
  display.drawLine(24, 42, 32, 38, SSD1306_WHITE);
  display.drawLine(32, 38, 40, 42, SSD1306_WHITE);
  display.drawLine(40, 42, 48, 38, SSD1306_WHITE);
  display.drawLine(48, 38, 56, 42, SSD1306_WHITE);
  display.drawLine(56, 42, 64, 38, SSD1306_WHITE);
  display.drawLine(64, 38, 72, 42, SSD1306_WHITE);
  display.fillCircle(96, 24, 3, SSD1306_WHITE);
  display.fillTriangle(94, 24, 98, 24, 96, 16, SSD1306_WHITE);
}
void drawFaceStuffy() {
  display.fillCircle(34, 26, 8, SSD1306_WHITE);
  display.fillCircle(34, 26, 4, SSD1306_BLACK);
  display.fillCircle(84, 26, 8, SSD1306_WHITE);
  display.fillCircle(84, 26, 4, SSD1306_BLACK);
  display.drawLine(36, 42, 44, 46, SSD1306_WHITE);
  display.drawLine(44, 46, 56, 40, SSD1306_WHITE);
  display.drawLine(56, 40, 68, 46, SSD1306_WHITE);
  display.drawLine(68, 46, 76, 42, SSD1306_WHITE);
  display.drawLine(26, 14, 30, 10, SSD1306_WHITE);
  display.drawLine(30, 10, 34, 14, SSD1306_WHITE);
  display.drawLine(76, 12, 80, 8,  SSD1306_WHITE);
  display.drawLine(80, 8,  84, 12, SSD1306_WHITE);
}
void drawFaceAlert() {
  display.fillCircle(34, 24, 12, SSD1306_WHITE);
  display.fillCircle(34, 25, 7,  SSD1306_BLACK);
  display.fillCircle(38, 20, 3,  SSD1306_WHITE);
  display.fillCircle(84, 24, 12, SSD1306_WHITE);
  display.fillCircle(84, 25, 7,  SSD1306_BLACK);
  display.fillCircle(88, 20, 3,  SSD1306_WHITE);
  display.fillCircle(59, 46, 6,  SSD1306_WHITE);
  display.fillCircle(59, 46, 4,  SSD1306_BLACK);
}
void drawFaceStressed() {
  display.drawLine(18, 14, 36, 20, SSD1306_WHITE);
  display.drawLine(18, 15, 36, 21, SSD1306_WHITE);
  display.drawLine(100, 14, 82, 20, SSD1306_WHITE);
  display.drawLine(100, 15, 82, 21, SSD1306_WHITE);
  display.fillCircle(34, 28, 8, SSD1306_WHITE);
  display.fillCircle(34, 28, 5, SSD1306_BLACK);
  display.fillCircle(84, 28, 8, SSD1306_WHITE);
  display.fillCircle(84, 28, 5, SSD1306_BLACK);
  display.drawCircle(59, 34, 11, SSD1306_WHITE);
  display.fillRect(36, 34, 46, 12, SSD1306_BLACK);
  display.drawLine(10, 24, 18, 32, SSD1306_WHITE);
  display.drawLine(108, 24, 100, 32, SSD1306_WHITE);
}
void drawFaceSleepy() {
  display.fillCircle(34, 28, 9, SSD1306_WHITE);
  display.fillRect(25, 20, 18, 8, SSD1306_BLACK);
  display.fillCircle(84, 28, 9, SSD1306_WHITE);
  display.fillRect(75, 20, 18, 8, SSD1306_BLACK);
  display.drawLine(44, 46, 74, 46, SSD1306_WHITE);
  display.drawLine(44, 47, 74, 47, SSD1306_WHITE);
  display.setTextSize(2);
  display.setCursor(96, 8);
  display.print("z");
  display.setTextSize(1);
  display.setCursor(108, 4);
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

  // separator line
  display.drawLine(0, 52, 127, 52, SSD1306_WHITE);

  // bottom bar
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(2, 56);
  display.print(face);

  // time
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
  if (air < 1500) return "EXCEL";
  if (air < 2000) return "GOOD";
  if (air < 2500) return "FAIR";
  return "POOR";
}

void drawEnvScreen(float temp, float humidity, int mq135, int wellness) {
  display.clearDisplay();

  // Title
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(34, 0);
  display.print("ENVIRONMENT");

  display.drawLine(0, 10, 127, 10, SSD1306_WHITE);

  // Three columns: TEMP | HUM | AIR
  // TEMP
  display.setCursor(2, 14);
  display.print("TEMP");
  display.setCursor(2, 24);
  display.setTextSize(1);
  display.print(temp, 1);
  display.print("\xF7");  // degree symbol

  // HUM
  display.setCursor(46, 14);
  display.print("HUM");
  display.setCursor(46, 24);
  display.print(humidity, 0);
  display.print("%");

  // AIR
  display.setCursor(88, 14);
  display.print("AIR");
  display.setCursor(88, 24);
  display.print(airLabel(mq135));

  display.drawLine(0, 35, 127, 35, SSD1306_WHITE);

  // Wellness bar
  display.setCursor(2, 39);
  display.print("Wellness");

  // Draw bar
  int barWidth = map(wellness, 0, 100, 0, 80);
  display.drawRect(2, 49, 82, 8, SSD1306_WHITE);
  display.fillRect(2, 49, barWidth, 8, SSD1306_WHITE);

  // Percentage
  display.setCursor(90, 49);
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
    // MORNING — rising sun
    int sunY = 40 - (animFrame * 3);
    if (sunY < 20) sunY = 20;
    display.fillCircle(64, sunY, 12, SSD1306_WHITE);
    // rays
    for (int i = 0; i < 8; i++) {
      float angle = i * 3.14159 / 4;
      int x1 = 64 + 16 * cos(angle);
      int y1 = sunY + 16 * sin(angle);
      int x2 = 64 + 22 * cos(angle);
      int y2 = sunY + 22 * sin(angle);
      display.drawLine(x1, y1, x2, y2, SSD1306_WHITE);
    }
    // horizon
    display.drawLine(0, 50, 127, 50, SSD1306_WHITE);
    display.setCursor(36, 54);
    display.setTextSize(1);
    display.print("Good morning");

  } else if (hour >= 12 && hour < 18) {
    // AFTERNOON — calm horizontal lines
    for (int i = 0; i < 5; i++) {
      int y = 10 + i * 12;
      int offset = (animFrame % 2 == 0) ? 0 : 4;
      display.drawLine(offset, y, 127 - offset, y, SSD1306_WHITE);
    }
    display.setCursor(30, 54);
    display.setTextSize(1);
    display.print("Good afternoon");

  } else if (hour >= 18 && hour < 22) {
    // EVENING — stars appearing
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
    // crescent moon
    display.fillCircle(100, 42, 10, SSD1306_WHITE);
    display.fillCircle(106, 38, 9, SSD1306_BLACK);
    display.setCursor(36, 54);
    display.setTextSize(1);
    display.print("Good evening");

  } else {
    // NIGHT — full stars + moon
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
// SCREEN 3B — MILO'S WORLD (pixel scene)
// ================================================================
void drawMiloWorld() {
  display.clearDisplay();

  // Floor
  display.drawLine(0, 56, 127, 56, SSD1306_WHITE);

  // Desk
  display.fillRect(20, 44, 88, 4, SSD1306_WHITE);
  display.fillRect(22, 48, 4, 8, SSD1306_WHITE);   // left leg
  display.fillRect(102, 48, 4, 8, SSD1306_WHITE);  // right leg

  // Milo sitting on desk — tiny face
  display.fillCircle(64, 36, 8, SSD1306_WHITE);
  display.fillCircle(61, 34, 2, SSD1306_BLACK);    // left eye
  display.fillCircle(67, 34, 2, SSD1306_BLACK);    // right eye
  display.drawLine(61, 39, 67, 39, SSD1306_BLACK); // smile

  // Window on wall (left)
  display.drawRect(4, 8, 24, 28, SSD1306_WHITE);
  display.drawLine(16, 8, 16, 36, SSD1306_WHITE);
  display.drawLine(4, 22, 28, 22, SSD1306_WHITE);

  // Animated stars/sun outside window
  if (animFrame % 2 == 0) {
    display.fillCircle(11, 15, 3, SSD1306_WHITE);
  } else {
    display.drawCircle(11, 15, 3, SSD1306_WHITE);
  }
  display.fillCircle(22, 28, 2, SSD1306_WHITE);

  // Plant (right side)
  display.drawLine(110, 56, 110, 40, SSD1306_WHITE);  // stem
  display.fillCircle(106, 38, 5, SSD1306_WHITE);       // leaf left
  display.fillCircle(114, 38, 5, SSD1306_WHITE);       // leaf right
  display.fillCircle(110, 36, 5, SSD1306_WHITE);       // top

  // Pot
  display.fillRect(106, 50, 8, 6, SSD1306_WHITE);

  // Label
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(38, 58);
  display.print("Milo's world");

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
  xTaskCreate(capture_samples, "CaptureSamples", 1024*32,
              (void*)sample_buffer_size, 10, NULL);
  return true;
}
static bool microphone_inference_record(void) {
  while (inference.buf_ready == 0) delay(10);
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
  display.print("v 1.3");
  display.setCursor(18, 52);
  display.print("smart companion");
  display.display();
  delay(2000);

  connectWiFi();
  mqtt.setServer(mqtt_server, mqtt_port);
  connectMQTT();

  if (!microphone_inference_start(EI_CLASSIFIER_RAW_SAMPLE_COUNT)) {
    Serial.println("ERR: Audio buffer failed!");
    while (true);
  }

  faceHoldTime = millis();
  lastScreenSwitch = millis();
  Serial.println("Milo v1.3 ready!");
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
    case 0: drawMoodScreen(lastFace, temp);               break;
    case 1: drawEnvScreen(temp, humidity, mq135, wellness); break;
    case 2: drawAmbientArt();                              break;
    case 3: drawMiloWorld();                               break;
  }

  // 6. MQTT publish
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
