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


// ── Pin definitions ──────────────────────────────────────────────
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

// ── Face hold timer ───────────────────────────────────────────────
#define FACE_HOLD_MS      3000
String lastFace     = "content";
unsigned long faceHoldTime = 0;

// ── PIR software timeout ──────────────────────────────────────────
unsigned long lastMotionTime = 0;

// ── MQTT publish interval ─────────────────────────────────────────
#define MQTT_INTERVAL     1000
unsigned long lastMqttTime = 0;

// ── Objects ──────────────────────────────────────────────────────
DHT dht(DHT_PIN, DHT22);
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
WiFiClient espClient;
PubSubClient mqtt(espClient);

// ── Audio inference structs ───────────────────────────────────────
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
// WIFI + MQTT FUNCTIONS
// ================================================================
void connectWiFi() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(20, 20);
  display.print("Connecting WiFi");
  display.display();

  for (int n = 0; n < networkCount; n++) {
    Serial.print("Trying: ");
    Serial.println(networks[n][0]);

    WiFi.begin(networks[n][0], networks[n][1]);

    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
      delay(500);
      attempts++;
      display.clearDisplay();
      display.setCursor(20, 12);
      display.print("Trying network");
      display.setCursor(4, 24);
      display.print(networks[n][0]);
      display.setCursor(48, 40);
      for (int i = 0; i < (attempts % 4); i++) display.print(". ");
      display.display();
    }

    if (WiFi.status() == WL_CONNECTED) {
      display.clearDisplay();
      display.setCursor(20, 20);
      display.print("WiFi connected!");
      display.setCursor(8, 36);
      display.print(WiFi.localIP());
      display.display();
      delay(2000);
      Serial.print("Connected to: ");
      Serial.println(networks[n][0]);
      Serial.print("IP: ");
      Serial.println(WiFi.localIP());
      return;
    }

    WiFi.disconnect();
    delay(500);
  }

  display.clearDisplay();
  display.setCursor(16, 20);
  display.print("No WiFi found");
  display.setCursor(8, 36);
  display.print("Check credentials");
  display.display();
  Serial.println("All networks failed!");
  delay(3000);
}

void connectMQTT() {
  while (!mqtt.connected()) {
    Serial.print("Connecting to MQTT...");
    if (mqtt.connect(mqtt_client)) {
      Serial.println("connected!");
      mqtt.publish("milo/status", "online");
    } else {
      Serial.print("failed, rc=");
      Serial.println(mqtt.state());
      delay(2000);
    }
  }
}

void publishData(float temp, float humidity, int mq135,
                 bool motion, String face, String topAudio) {
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
  mqtt.publish("milo/audio",    topAudio.c_str());
}

// ================================================================
// FACE DRAWING FUNCTIONS
// ================================================================
void drawContent() {
  display.fillCircle(38, 28, 10, SSD1306_WHITE);
  display.fillCircle(38, 29, 5,  SSD1306_BLACK);
  display.fillCircle(41, 25, 2,  SSD1306_WHITE);
  display.fillCircle(90, 28, 10, SSD1306_WHITE);
  display.fillCircle(90, 29, 5,  SSD1306_BLACK);
  display.fillCircle(93, 25, 2,  SSD1306_WHITE);
  display.drawCircle(64, 44, 12, SSD1306_WHITE);
  display.fillRect(40, 32, 48, 14, SSD1306_BLACK);
}
void drawWarm() {
  display.drawLine(28, 28, 48, 24, SSD1306_WHITE);
  display.drawLine(28, 29, 48, 25, SSD1306_WHITE);
  display.drawLine(80, 24, 100, 28, SSD1306_WHITE);
  display.drawLine(80, 25, 100, 29, SSD1306_WHITE);
  display.drawLine(44, 48, 52, 44, SSD1306_WHITE);
  display.drawLine(52, 44, 60, 48, SSD1306_WHITE);
  display.drawLine(60, 48, 68, 44, SSD1306_WHITE);
  display.drawLine(68, 44, 76, 48, SSD1306_WHITE);
  display.drawLine(76, 48, 84, 44, SSD1306_WHITE);
  display.fillCircle(104, 26, 3,  SSD1306_WHITE);
  display.fillTriangle(102, 26, 106, 26, 104, 18, SSD1306_WHITE);
}
void drawStuffy() {
  display.fillCircle(38, 28, 9, SSD1306_WHITE);
  display.fillCircle(38, 28, 4, SSD1306_BLACK);
  display.fillCircle(90, 28, 9, SSD1306_WHITE);
  display.fillCircle(90, 28, 4, SSD1306_BLACK);
  display.drawLine(44, 46, 52, 50, SSD1306_WHITE);
  display.drawLine(52, 50, 64, 44, SSD1306_WHITE);
  display.drawLine(64, 44, 76, 50, SSD1306_WHITE);
  display.drawLine(76, 50, 84, 46, SSD1306_WHITE);
  display.drawLine(30, 16, 34, 12, SSD1306_WHITE);
  display.drawLine(34, 12, 38, 16, SSD1306_WHITE);
  display.drawLine(82, 16, 86, 12, SSD1306_WHITE);
  display.drawLine(86, 12, 90, 16, SSD1306_WHITE);
}
void drawAlert() {
  display.fillCircle(38, 26, 13, SSD1306_WHITE);
  display.fillCircle(38, 27, 7,  SSD1306_BLACK);
  display.fillCircle(42, 22, 3,  SSD1306_WHITE);
  display.fillCircle(90, 26, 13, SSD1306_WHITE);
  display.fillCircle(90, 27, 7,  SSD1306_BLACK);
  display.fillCircle(94, 22, 3,  SSD1306_WHITE);
  display.fillCircle(64, 50, 7,  SSD1306_WHITE);
  display.fillCircle(64, 50, 4,  SSD1306_BLACK);
}
void drawStressed() {
  display.drawLine(24, 14, 48, 20, SSD1306_WHITE);
  display.drawLine(24, 15, 48, 21, SSD1306_WHITE);
  display.drawLine(80, 20, 104, 14, SSD1306_WHITE);
  display.drawLine(80, 21, 104, 15, SSD1306_WHITE);
  display.fillCircle(38, 30, 9, SSD1306_WHITE);
  display.fillCircle(38, 30, 5, SSD1306_BLACK);
  display.fillCircle(90, 30, 9, SSD1306_WHITE);
  display.fillCircle(90, 30, 5, SSD1306_BLACK);
  display.drawCircle(64, 36, 12, SSD1306_WHITE);
  display.fillRect(40, 36, 48, 14, SSD1306_BLACK);
  display.drawLine(14, 26, 22, 34, SSD1306_WHITE);
  display.drawLine(114, 26, 106, 34, SSD1306_WHITE);
}
void drawSleepy() {
  display.fillCircle(38, 30, 10, SSD1306_WHITE);
  display.fillRect(28, 20, 20, 10, SSD1306_BLACK);
  display.fillCircle(90, 30, 10, SSD1306_WHITE);
  display.fillRect(80, 20, 20, 10, SSD1306_BLACK);
  display.drawLine(50, 50, 78, 50, SSD1306_WHITE);
  display.drawLine(50, 51, 78, 51, SSD1306_WHITE);
  display.setTextSize(2);
  display.setCursor(100, 10);
  display.print("z");
  display.setTextSize(1);
  display.setCursor(112, 4);
  display.print("z");
}

void renderFace(String face, float temp) {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  if      (face == "alert")    drawAlert();
  else if (face == "stressed") drawStressed();
  else if (face == "stuffy")   drawStuffy();
  else if (face == "warm")     drawWarm();
  else if (face == "sleepy")   drawSleepy();
  else                         drawContent();

  display.setCursor(0, 56);
  display.print(face);
  display.setCursor(70, 56);
  display.print(temp, 1);
  display.print("C");
  display.display();
}

// ================================================================
// FACE DECISION ENGINE
// ================================================================
String decideFace(float temp, float humidity, int mq135,
                  bool motion, float conf_noise,
                  float conf_keyboard, float conf_silence) {
  if (conf_keyboard >= AUDIO_KEYBOARD)                        return "stressed";
  if (conf_noise    >= AUDIO_NOISE)                           return "alert";
  if (temp > TEMP_STUFFY || humidity > HUMIDITY_STUFFY
      || mq135 > MQ135_STUFFY)                               return "stuffy";
  if (temp > TEMP_WARM)                                      return "warm";
  if (!motion)                                               return "sleepy";
  return "content";
}

// ================================================================
// I2S AUDIO FUNCTIONS
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
      for (int x = 0; x < i2s_bytes_to_read / 2; x++)
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
    .mode                 = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX | I2S_MODE_TX),
    .sample_rate          = EI_CLASSIFIER_FREQUENCY,
    .bits_per_sample      = (i2s_bits_per_sample_t)16,
    .channel_format       = I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = I2S_COMM_FORMAT_I2S,
    .intr_alloc_flags     = 0,
    .dma_buf_count        = 8,
    .dma_buf_len          = 512,
    .use_apll             = false,
    .tx_desc_auto_clear   = false,
    .fixed_mclk           = -1,
  };
  i2s_pin_config_t pin_config = {
    .bck_io_num   = I2S_SCK,
    .ws_io_num    = I2S_WS,
    .data_out_num = -1,
    .data_in_num  = I2S_SD
  };

  i2s_driver_install((i2s_port_t)1, &i2s_config, 0, NULL);
  i2s_set_pin((i2s_port_t)1, &pin_config);
  i2s_zero_dma_buffer((i2s_port_t)1);
  ei_sleep(100);
  record_status = true;
  xTaskCreate(capture_samples, "CaptureSamples", 1024 * 32,
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
  display.print("v 1.2");
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
  Serial.println("Milo v1.2 ready!");
  Serial.println("Face | Temp | Hum | Air | PIR | kbd | noise | sil");
}

// ================================================================
// LOOP
// ================================================================
void loop() {
  float temp     = dht.readTemperature();
  float humidity = dht.readHumidity();
  int   mq135    = analogRead(MQ135_AO);

  bool rawMotion = digitalRead(PIR_PIN);
  if (rawMotion) lastMotionTime = millis();
  bool motion = (millis() - lastMotionTime) < MOTION_TIMEOUT;

  if (isnan(temp) || isnan(humidity)) {
    Serial.println("DHT22 error");
    delay(2000);
    return;
  }

  microphone_inference_record();

  signal_t signal;
  signal.total_length = EI_CLASSIFIER_RAW_SAMPLE_COUNT;
  signal.get_data     = &microphone_audio_signal_get_data;
  ei_impulse_result_t result = { 0 };
  run_classifier(&signal, &result, false);

  float conf_keyboard = 0, conf_noise = 0,
        conf_silence  = 0, conf_voice = 0;
  String topAudio = "silence";
  float  topConf  = 0;

  for (size_t ix = 0; ix < EI_CLASSIFIER_LABEL_COUNT; ix++) {
    String label = result.classification[ix].label;
    float  val   = result.classification[ix].value;
    if      (label == "keyboard") conf_keyboard = val;
    else if (label == "noise")    conf_noise    = val;
    else if (label == "silence")  conf_silence  = val;
    else if (label == "voice")    conf_voice    = val;
    if (val > topConf) { topConf = val; topAudio = label; }
  }

  String newFace = decideFace(temp, humidity, mq135, motion,
                               conf_noise, conf_keyboard, conf_silence);

  if (newFace != lastFace) {
    if (millis() - faceHoldTime >= FACE_HOLD_MS) {
      lastFace = newFace;
      faceHoldTime = millis();
      renderFace(lastFace, temp);
    }
  } else {
    faceHoldTime = millis();
    renderFace(lastFace, temp);
  }

  publishData(temp, humidity, mq135, motion, lastFace, topAudio);

  Serial.print(lastFace);        Serial.print(" | ");
  Serial.print(temp, 1);         Serial.print("C | ");
  Serial.print(humidity, 1);     Serial.print("% | ");
  Serial.print(mq135);           Serial.print(" | ");
  Serial.print(motion ? "Y":"N");Serial.print(" | ");
  Serial.print(conf_keyboard, 3);Serial.print(" | ");
  Serial.print(conf_noise, 3);   Serial.print(" | ");
  Serial.println(conf_silence, 3);
}

#if !defined(EI_CLASSIFIER_SENSOR) || \
    EI_CLASSIFIER_SENSOR != EI_CLASSIFIER_SENSOR_MICROPHONE
#error "Invalid model for current sensor."
#endif
