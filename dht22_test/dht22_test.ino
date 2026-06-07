#include <DHT.h>
#define DHT_PIN 4
#define DHT_TYPE DHT22
DHT dht(DHT_PIN, DHT_TYPE);
void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("DHT22 test starting...");
  dht.begin();
}
void loop() {
  delay(2000);
  float humidity    = dht.readHumidity();
  float temperature = dht.readTemperature();
  if (isnan(humidity) || isnan(temperature)) {
    Serial.println("ERROR: Failed to read from DHT22");
    Serial.println("Check: red->3V3, green->GPIO4, black->GND, 10k resistor");
    return;
  }
  Serial.print("Temperature: ");
  Serial.print(temperature);
  Serial.println(" C");
  Serial.print("Humidity:    ");
  Serial.print(humidity);
  Serial.println(" %");
  Serial.println("---");
}
