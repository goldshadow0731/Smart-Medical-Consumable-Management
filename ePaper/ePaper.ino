//https://dl.espressif.com/dl/package_esp32_index.json
//https://andestech.github.io/Arduino/package_Corvette_knectme_index.json

/* Import */
#include <WiFi.h>
#include <ESPmDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <SPI.h>
#include <ArduinoJson.h>
#include <PubSubClient.h>
#include "epd7in5b.h"
#include "epd7in5b_ext.h"
#include "error_image.h"

/* Public variable */
// WiFi
const char *ssid = "2706";
const char *password = "rootroot";

// MQTT Broker
const char *mqtt_server = "192.168.1.97";
const int mqtt_port = 1883;

// 光遮斷器感測
const char *topic0 = "light";
int buttonpin = 13;
int tmp = 0;

// Image
unsigned char IMAGE[61440] PROGMEM;
int stride = 60;
int package = 1024;
int pkg_count = 0;
bool drawable = false;

// Objects
Epd epd;
StaticJsonDocument<200> doc;
WiFiClient espclient;
PubSubClient client(espclient);

TaskHandle_t display_task;
TaskHandle_t cabinet_task;

// xTaskCreatePinnedToCore();
// xTaskCreate
// vTaskStartScheduler();


/* Function */
void Display(void * pvParameters) {
  Serial.println("Start draw");
  if (drawable == true){
    DisplayFrame(IMAGE);
  }
  else {
    DisplayQuarterFrame(ERROR_IMAGE);
  }
  Serial.println("End draw");
}

void CabinetStatus(void * pvParameters) {
  /* 光遮斷 */
  Serial.println("CabinetStatus start");
  if (digitalRead(buttonpin) == LOW){  // 開櫃子
    if (tmp == 0){
      tmp = millis();
    }
    else if (millis() - tmp >= 300000) {
      client.publish(topic0, "HIGH");  // 發布MQTT主題與訊息
    }
  }
  else{  // 櫃子關
    tmp = 0;
  }
  Serial.println("CabinetStatus end");
}

void callback(char* topic, byte* payload, unsigned int length) {
  if (topic == "clean") {
    for (long i=0; i<61440; i++) {
      IMAGE[i] = 0xff;
    }
  }
  else {
    int num = GetTopicNumber(topic, "/");
    Serial.print("Num: ");
    Serial.println(num);
    
    if (num == 0) {
      deserializeJson(doc, (char*)payload);

      const char* tmp_1 = doc["stride"];
      stride = ConstCharToInt(tmp_1);

      const char* tmp_2 = doc["package"];
      package = ConstCharToInt(tmp_2);

      pkg_count = 0;
    }
    else {
      drawable = false;
      int count = 0;
      int val;
      for(int i=0; i<length; i++){
        if ((int)payload[i] >= 192) {
          val = (payload[i] & 0x03) << 6;
          i++;
          val = (int)(val | payload[i]);
        }
        else {
          val = (int)payload[i];
        }
        IMAGE[(num-1)*stride+count] = val;
        count++;
      }

      pkg_count++;
      if (num == 1024 && pkg_count != 1024) {
        client.publish("Error/drawA", (char*)(package - pkg_count));
      }
      else if (num == 1024 && pkg_count == 1024) {
        drawable = true;
      }
    }
  }
}

void setup() {
  Serial.begin(115200); 
  pinMode(buttonpin, INPUT);
  
  //WiFi connect
  Serial.print("CONNECTING");
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());

  ArduinoOTA.setHostname("esp32");
  ArduinoOTA.setPassword("admin");
  ArduinoOTA
    .onStart([]() {
      String type;
      if (ArduinoOTA.getCommand() == U_FLASH)
        type = "sketch";
      else // U_SPIFFS
        type = "filesystem";

      // NOTE: if updating SPIFFS this would be the place to unmount SPIFFS using SPIFFS.end()
      Serial.println("Start updating " + type);
    })
    .onEnd([]() {
      Serial.println("\nEnd");
    })
    .onProgress([](unsigned int progress, unsigned int total) {
      Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
    })
    .onError([](ota_error_t error) {
      Serial.printf("Error[%u]: ", error);
      if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
      else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
      else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
      else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
      else if (error == OTA_END_ERROR) Serial.println("End Failed");
    });
  ArduinoOTA.begin();

  //設定客戶端，指定欲連接的MQTT伺服器的IP位址或網域名稱，以及埠號
  client.setServer(mqtt_server, mqtt_port);
  while (!client.connected()) {
    // connect 連線到MQTT伺服器，並傳入自訂的唯一識別碼
    if (client.connect("ESP32Client")) {
        Serial.println("Public emqx mqtt broker connected");
    } else {
        Serial.print("failed with state ");
        Serial.print(client.state());
        delay(2000);
    }
  }
  client.setCallback(callback);
  client.subscribe("drawA/#");

  // E-Paper
  if (epd.Init() != 0) {
      Serial.print("e-Paper init failed");
      return;
  }
  Serial.println("Start clean");
  epd.Clean();
  Serial.println("End clean");

  cabinet_task = xTimerCreate(
    "Cabinet",
    pdMS_TO_TICKS(1000),  // 1000ms
    pdTRUE,
    (void*)1,
    CabinetStatus
  );
  display_task = xTimerCreate(
    "Display",
    pdMS_TO_TICKS(60000),  // 60000ms
    pdTRUE,
    (void*)2,
    Display
  );
  xTimerStart(cabinet_task, 0);
  xTimerStart(display_task, pdMS_TO_TICKS(60000));
}

void loop() {
  client.loop(); // 更新用戶端狀態
  ArduinoOTA.handle();
  vTaskDelay(10);
}