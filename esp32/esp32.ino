/* Import */
#include <WiFi.h>
#include <ESPmDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <PubSubClient.h>
#include <rom/miniz.h>
#include <mbedtls/md5.h>


/* Preprocessor */
// Device
#define HOSTNAME_PREFIX "ESP32_"

// WiFi
#define WIFI_SSID "2706"
#define WIFI_PASSWORD "rootroot"
#define OFFSET_SEC 8 * 60 * 60

// OTA
#define OTA_PASSWORD "admin"

// MQTT
#define MQTT_SERVER "192.168.1.97"
#define MQTT_PORT 1883
#define EPAPER_TOPIC_PREFIX "draw/"
#define EPAPER_TOPIC_INIT_SUFFIX "/init"
#define EPAPER_TOPIC_PAYLOAD_SUFFIX "/payload"
#define EPAPER_TOPIC_STATUS_SUFFIX "/status"

// Cabinet
#define CABINET_INFIX "A"

// E-Paper
#define GET_DATA_PIN GPIO_NUM_4

// Sleep
#define WAKEUP_PIN GPIO_NUM_2

/* Variable */
// MQTT payload
bool send_init = false;
bool send_not_close = false;
unsigned short msg_ofs = 0;

// Image
const byte image_id_length = 8;
uint8_t image_uid[image_id_length] = {0};

// Objects
WiFiClient espclient;
PubSubClient client(espclient);


/* Function */
void get_md5(unsigned char * input, size_t ilen, unsigned char * decrypt){
  mbedtls_md5_context md5_ctx;
  mbedtls_md5_init(&md5_ctx);
  mbedtls_md5_starts(&md5_ctx);
  mbedtls_md5_update(&md5_ctx, input, ilen);
  mbedtls_md5_finish(&md5_ctx, decrypt);
  mbedtls_md5_free(&md5_ctx);
}

/*
  7 bit: Done
  6 bit: Success
  5 bit: Transport error
  4 bit: MD5 check error
  3 bit: Error image ID
  2 bit: Decompress error
  1 bit: Error data
  0 bit: Unable to allocate required memory
*/
enum callback_status {
  CALLBACK_STATUS_DONE = 0x80,
  CALLBACK_STATUS_SUCCESS = 0x40,
  CALLBACK_STATUS_TRANSPORT_ERROR = 0x20,
  CALLBACK_STATUS_MD5_CHECK_ERROR = 0x10,
  CALLBACK_STATUS_ERROR_IMAGE_ID = 0x08,
  CALLBACK_STATUS_DECOMPRESS_ERROR = 0x04,
  CALLBACK_STATUS_ERROR_DATA = 0x02,
  CALLBACK_STATUS_MEMORY_ERROR = 0x01,
  CALLBACK_STATUS_OK = 0x00
};

enum request_status {
  REQUEST_STATUS_OK = 0x00
};

void get_data() {
  char topic[20] = {'\0'};

  /* Init */
  strcpy(topic, "");
  strcat(topic, EPAPER_TOPIC_PREFIX);
  strcat(topic, CABINET_INFIX);
  strcat(topic, EPAPER_TOPIC_INIT_SUFFIX);

  byte tmp_1;  // size
  char tmp_2[5];  // data
  char init_payload[20] = {'\0'};
  utoa(client.getBufferSize(), tmp_2, 10);  // Buffer size
  init_payload[0] = strlen(tmp_2);
  strcat(init_payload, tmp_2);
  Serial.println(init_payload);
  Serial2.write(init_payload, strlen(init_payload));
  size_t tmp_3 = strlen(init_payload);

  while (true) {
    if (Serial2.available()) {
      Serial2.readBytes(init_payload, 20);
      break;
    }
  }
  if (tmp_3 != strlen(init_payload)) {
    send_init = true;
    Serial.println(init_payload);
    client.publish(topic, init_payload);
  }
}

void callback(char *topic, byte *payload, unsigned int length) {
  // Request payload - draw/payload/<cabinet>
  //       3 Byte seq       (24 bit number)
  //       1 Byte flags     (4 bit not use (padding 0) + 1 bit zip + 1 bit syn + 1 bit fin + 1 bit ack)
  //       3 Byte ack       (24 bit number)
  //       1 Byte status    (ref. request_status)
  //       8 Byte image_uid (showflake plus)
  //      16 Byte checksum  (MD5)
  // +   xxx Byte data
  // -------------------------
  //  xxx+32 Byte total
  // =========================
  // Response payload - draw/status/<cabinet>
  //       3 Byte seq       (24 bit number)
  //       1 Byte flags     (4 bit not use (padding 0) + 1 bit zip + 1 bit syn + 1 bit fin + 1 bit ack)
  //       3 Byte ack       (24 bit number)
  //       1 Byte status    (ref. callback_status)
  //       8 Byte image_uid (showflake plus)
  //      16 Byte checksum  (MD5)
  // +   xxx Byte data      (init only)
  // -------------------------
  //  xxx+32 Byte total

  char resp_topic[20];
  strcpy(resp_topic, "");
  strcat(resp_topic, EPAPER_TOPIC_PREFIX);
  strcat(resp_topic, CABINET_INFIX);
  strcat(resp_topic, EPAPER_TOPIC_STATUS_SUFFIX);

  randomSeed(analogRead(GPIO_NUM_0));
  const byte md5_length = 16;
  uint8_t resp_seq[3] = {0};
  uint8_t resp_ack[3] = {0};
  uint8_t resp_flags = 0x00;
  callback_status resp_status = CALLBACK_STATUS_OK;

  // Check payload size
  if (length < 32) {
    uint32_t tmp_seq = (unsigned int)random(0x00000000, 0x00ffffff);
    uint32_t tmp_ack = (unsigned int)random(0x00000000, 0x00ffffff);
    for (byte i=0; i<3; i++) {
      resp_seq[i] = (tmp_seq >> (8 * (2 - i)));
      resp_ack[i] = (tmp_ack >> (8 * (2 - i)));
    }
    resp_status = CALLBACK_STATUS_ERROR_DATA;  // 1 bit: Error data
  }
  else {
    // Parser request value
    uint8_t req_seq[3] = {payload[0], payload[1], payload[2]};
    uint8_t req_flags = payload[3];
    uint8_t req_ack[3] = {payload[4], payload[5], payload[6]};
    uint8_t req_status = payload[7];
    uint8_t req_image_id[image_id_length];
    for (byte i=0; i<image_id_length; i++)
      req_image_id[i] = payload[8+i];
    unsigned char md5_value[md5_length];
    for (byte i=0; i<md5_length; i++) {
      md5_value[i] = payload[16+i];
      payload[16+i] = 0;
    }

    // Ready response value
    for (byte i=0; i<3; i++) {
      resp_seq[i] = req_ack[i];
      resp_ack[i] = req_seq[i];
    }
    if ((req_flags >> 2) & 1) {  // SYN
      msg_ofs = 0;
      for (byte i=0; i<image_id_length; i++)
        image_uid[i] = req_image_id[i];
      uint32_t tmp_seq = (unsigned int)random(0x00000000, 0x00ffffff);
      for (byte i=0; i<3; i++)
        resp_seq[i] = (tmp_seq >> (8 * (2 - i)));
      resp_flags |= 0x04;
    }

    // Check MD5
    if (resp_status == CALLBACK_STATUS_OK) {
      unsigned char decrypt[md5_length] = {0};
      get_md5(payload, length, decrypt);
      for (byte i=0; i<md5_length; i++) {
        if (decrypt[i] ^ md5_value[i]) {
          resp_status = CALLBACK_STATUS_MD5_CHECK_ERROR;  // 4 bit: MD5 check error
          break;
        }
      }
    }

    // Check Image ID
    if (resp_status == CALLBACK_STATUS_OK) {
      for (byte i=0; i<image_id_length; i++) {
        if (image_uid[i] ^ req_image_id[i]) {
          resp_status = CALLBACK_STATUS_ERROR_IMAGE_ID;  // 3 bit: Error image ID
          break;
        }
      }
    }

    // Get payload and decompress
    if (resp_status == CALLBACK_STATUS_OK) {
      Serial2.write(payload[3]);
      for (int i=32; i<length; i++)
        Serial2.write(payload[i]);
      Serial2.flush();
      while (true) {
        if (Serial2.available()) {
          int result = Serial2.read();
          if (result == 0 && (payload[3] >> 1) & 1) {
            resp_status = CALLBACK_STATUS_DONE;
            send_init = false;
          }
          else if (result == 0)
            resp_status = CALLBACK_STATUS_OK;
          else
            resp_status = CALLBACK_STATUS_DECOMPRESS_ERROR;  // 2 bit: Decompress error
          break;
        }
      }
    }
  }

  // Conbine response
  if (resp_status == CALLBACK_STATUS_OK) {
    resp_status = CALLBACK_STATUS_SUCCESS;
    uint32_t tmp_ack = 0;
    for (byte i=0; i<3; i++)
      tmp_ack = tmp_ack << 8 | resp_ack[i];
    tmp_ack += (length - 32);
    for (byte i=0; i<3; i++)
      resp_ack[i] = (tmp_ack >> (8 * (2 - i)));
  }

  char resp_data[32] = {0};
  for (byte i=0; i<3; i++) {
    resp_data[i] = resp_seq[i];
    resp_data[4+i] = resp_ack[i];
  }
  resp_data[3] = resp_flags;
  resp_data[7] = resp_status;
  for (byte i=0; i<8; i++)
    resp_data[8+i] = image_uid[i];

  unsigned char decrypt[md5_length] = {0};
  get_md5((unsigned char *)resp_data, 32, decrypt);
  for (byte i=0; i<md5_length; i++)
    resp_data[16+i] = decrypt[i];

  client.publish(resp_topic, (const uint8_t*)resp_data, 32);
}

void setup() {
  esp_sleep_enable_ext0_wakeup(WAKEUP_PIN, LOW);

  /* Variable */
  // Device ID
  char device_id[10];
  strcpy(device_id, "");
  strcat(device_id, HOSTNAME_PREFIX);
  strcat(device_id, CABINET_INFIX);

  /* Serial */
  Serial.begin(115200);
  Serial2.begin(115200);

  /* WiFi */
  Serial.println("Connecting...");
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  if (WiFi.waitForConnectResult() != WL_CONNECTED) {
    ESP.restart();
  }
  WiFi.setAutoReconnect(true);
  WiFi.persistent(true);
  Serial.println("WiFi connected");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

  configTime(OFFSET_SEC, 0, "pool.ntp.org", "tw.pool.ntp.org");

  /* mDNS */
  if (!MDNS.begin(device_id)) {
    Serial.println("Error setting up MDNS responder!");
  }

  /* OTA */
  ArduinoOTA.setHostname(device_id);
  ArduinoOTA.setPassword(OTA_PASSWORD);
  ArduinoOTA
    .onStart([]() {
      String type = (ArduinoOTA.getCommand() == U_FLASH) ? "sketch" : "filesystem"; // U_SPIFFS
      // NOTE: if updating SPIFFS this would be the place to unmount SPIFFS using SPIFFS.end()
      Serial.println("Start updating " + type);
    })
    .onEnd([]() {
      Serial.println("\nEnd");
    })
    .onProgress([](unsigned int progress, unsigned int total) {
      Serial.printf("Progress: %u%%\n", (progress / (total / 100)));
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

  /* MQTT */
  // client.setBufferSize(512);
  client.setServer(MQTT_SERVER, MQTT_PORT);
  for (int i=0; i<5; i++) {
    if (client.connect(device_id))
      Serial.println("MQTT broker connected");
      break;
  }
  if (!client.connected()) {
    Serial.print("failed with state ");
    Serial.println(client.state());
    ESP.restart();
  }
  client.setCallback(callback);
  // MQTT Topic
  char topic[20] = {'\0'};
  strcpy(topic, "");
  strcat(topic, EPAPER_TOPIC_PREFIX);
  strcat(topic, CABINET_INFIX);
  strcat(topic, EPAPER_TOPIC_PAYLOAD_SUFFIX);
  Serial.printf("Subscribe topic: %s\n", topic);
  client.subscribe(topic, 1);
}

void loop() {
  if (digitalRead(GET_DATA_PIN) == LOW) {
    if (send_init == false)
      get_data();
  }
  else {
    if (send_not_close == false) {
      Serial.println("cabinet");
      client.publish("cabinet", "cabinet");
      send_not_close = true;
      delay(1000);
    }
  }
  client.loop();
  ArduinoOTA.handle();
  if (digitalRead(WAKEUP_PIN) == HIGH) {
    Serial.println("Enter sleep");
    esp_deep_sleep_start();
  }
  vTaskDelay(10);
}
