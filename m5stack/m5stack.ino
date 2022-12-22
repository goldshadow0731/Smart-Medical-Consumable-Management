/* Import */
#include <ArduinoJson.h>
#include <ArduinoOTA.h>
#include <M5Atom.h>
#include <WiFi.h>
#include <PubSubClient.h>


/* Preprocessor */
// Device
#define HOSTNAME "M5Atom_QR-code"

// WiFi
#define WIFI_SSID "2706"
#define WIFI_PASSWORD "rootroot"

// OTA
#define OTA_PASSWORD "admin"

// MQTT
#define MQTT_SERVER "192.168.1.97"
#define MQTT_PORT 1883

// QR-code reader
#define TRIG 23
#define DLED 33


/* Variable */
// Objects
char device_id[20];
String code = "";
bool sub_topic = true;
unsigned long start_time = 0;

StaticJsonDocument<512> doc;
WiFiClient espClient;
PubSubClient client(espClient);


void setup() {
  M5.begin(true, false, true);
  M5.dis.fillpix(0xff0000);

  strcpy(device_id, "");
  strcat(device_id, HOSTNAME);
  strcat(device_id, "_");
  char tmp[5];
  randomSeed(digitalRead(GPIO_NUM_0));
  ltoa(random(0xffff), tmp, HEX);
  strcat(device_id, tmp);
  Serial.println(device_id);

  /* QR-code */
  Serial2.begin(9600, SERIAL_8N1, 22, 19);
  pinMode(TRIG, OUTPUT);
  pinMode(DLED, INPUT);
  digitalWrite(TRIG, HIGH);

  /* WiFi */
  Serial.printf("Connecting to %s\n", WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  if (WiFi.waitForConnectResult() != WL_CONNECTED)
    ESP.restart();
  Serial.println("Success");

  /* OTA */
  ArduinoOTA.setHostname(device_id);
  ArduinoOTA.setPassword(OTA_PASSWORD);
  ArduinoOTA.begin();

  /* MQTT */
  client.setServer(MQTT_SERVER, MQTT_PORT);
}

void loop() {
  while (!client.connected()) {
    M5.dis.fillpix(0xffff00);
    if (!client.connect(device_id)) {
      Serial.printf("failed, rc=%d\n", client.state());
      Serial.println("try again in 5 seconds");
      delay(5000);
    }
    else
      Serial.println("connected");
  }
  M5.dis.fillpix(0x00ff00);
  client.loop();
  ArduinoOTA.handle();

  M5.update();
  digitalWrite(TRIG, !(M5.Btn.isPressed()));

  if (M5.Btn.isPressed() && digitalRead(DLED) == HIGH) {
    M5.dis.fillpix(0x00ffff);
    if (Serial2.available() > 0) {
      code = Serial2.readString();
      Serial.println(code);
      if (code == "ADDNETMEDICALSUPPLY")
        sub_topic = false;
      else {
        start_time = millis();
        if (doc.containsKey(code))
          doc[code] = doc[code].as<int>() + 1;
        else
          doc[code] = 1;
      }
      code = "";
    }
  }

  if (millis() - start_time > 5000 && !(doc.isNull())) {
    doc["add_type"] = sub_topic ? 0 : 1;
    serializeJson(doc, code);
    Serial.println(code);
    client.publish("try/test", code.c_str());
    sub_topic = true;
    code = "";
    doc.clear();
  }
}