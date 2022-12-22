#define MINIZ_NO_TIME


/* Import */
#include <SPI.h>
#include "epd7in5b.h"
#include "epd7in5b_ext.h"
#include "miniz/miniz.c"


/* Preprocessor */
// E-Paper
#define EPD_COLOR_DEEPTH 2
#ifndef EPD_COLOR_DEEPTH
  #define IMAGE_LENGTH EPD_WIDTH * EPD_HEIGHT / 8
#else
  #define IMAGE_LENGTH EPD_WIDTH * EPD_HEIGHT / 8 * EPD_COLOR_DEEPTH
#endif

// ESP32
#define WAKEUP_PIN 5
#define EPAPER_PIN 4

// 光遮斷
#define LIGHT_PIN 6


/* Public variable */
// 光遮斷器感測
bool just_open = false;
unsigned int open_time = 0;

// payload
unsigned char * message = NULL;
unsigned short msg_ofs = 0;
tinfl_decompressor* decomp = NULL;
unsigned int serial_buffer_size = 0;

// Image
unsigned char IMAGE[IMAGE_LENGTH] PROGMEM = {'\0'};

// Objects
Epd epd;


void setup() {
  pinMode(WAKEUP_PIN, OUTPUT);
  pinMode(EPAPER_PIN, OUTPUT);
  pinMode(LIGHT_PIN, INPUT);
  digitalWrite(EPAPER_PIN, HIGH);
  digitalWrite(WAKEUP_PIN, HIGH);

  /* Serial */
  Serial.begin(115200);
  Serial1.begin(115200);

  // E-Paper
  if (epd.Init() != 0) {
    Serial.print("e-Paper init failed");
    return;
  }
  Serial.println("Start clean");
  epd.Clean();
  Serial.println("End clean");

  Serial.println(IMAGE_LENGTH);
}

void loop() {
  /* 光遮斷 */
  if (digitalRead(LIGHT_PIN) == HIGH){ // 關櫃子
    if (just_open == true) {
      Serial.println("close");
      open_time = 0;
      just_open = false;
      // get data
      digitalWrite(EPAPER_PIN, LOW);
      digitalWrite(WAKEUP_PIN, LOW);
      delay(10000);
      serial_buffer_size = 0;
      char payload[20] = {'\0'};
      Serial1.readBytes(payload, 20);
      for (byte i=1; i<=(byte)payload[0]; i++)
        serial_buffer_size = serial_buffer_size * 10 + (payload[i] - '0');
      Serial.println(serial_buffer_size);
      for (byte i=(byte)((byte)payload[0] + 1); i<20; i++)
        payload[i] = '\0';

      message = (unsigned char *) calloc(serial_buffer_size, sizeof(unsigned char));
      if (message == NULL)
        Serial1.write(payload);
      else {
        byte tmp_1;  // size
        char tmp_2[10];  // data
        size_t tmp_3 = 0;
        utoa(EPD_WIDTH, tmp_2, 10);  // Width
        tmp_1 = strlen(tmp_2);
        tmp_3 = strlen(payload);
        payload[tmp_3++] = tmp_1;
        strcat(payload, tmp_2);
        utoa(EPD_HEIGHT, tmp_2, 10);  // Height
        tmp_1 = strlen(tmp_2);
        tmp_3 = strlen(payload);
        payload[tmp_3++] = tmp_1;
        strcat(payload, tmp_2);
        Serial.println(payload);
        Serial1.write(payload);
      }
    }
  }
  else {  // 開櫃子
    if (just_open == false) {
      just_open = true;
      Serial.println("open");
      open_time = millis();
    }
    else if (millis() - open_time >= 30000) {
      digitalWrite(EPAPER_PIN, HIGH);
      digitalWrite(WAKEUP_PIN, LOW);
      open_time = millis();
      digitalWrite(WAKEUP_PIN, HIGH);
    }
  }

  if (Serial1.available()) {
    byte flags = Serial1.read();

    if ((flags >> 2) & 1) {
      msg_ofs = 0;
      decomp = (tinfl_decompressor *) malloc(sizeof(tinfl_decompressor));
      if (decomp == NULL)
        Serial1.write(0xff);
      else
        tinfl_init(decomp);
    }

    if (message == NULL) {
      while (Serial1.available())
        Serial1.read();
    }
    else {
      size_t len = Serial1.readBytes(message, serial_buffer_size);

      // Decompress
      tinfl_status status = TINFL_STATUS_FAILED;
      mz_uint32 decomp_flags = TINFL_FLAG_PARSE_ZLIB_HEADER | TINFL_FLAG_HAS_MORE_INPUT | TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF;
      size_t in_size = 0, out_size = 0, in_ofs = 0;
      while (in_size != len) {
        in_size = len - in_ofs;
        out_size = IMAGE_LENGTH - msg_ofs;
        status = tinfl_decompress(decomp, (const mz_uint8 *)&message[in_ofs], &in_size, (mz_uint8 *)IMAGE, (mz_uint8 *)&IMAGE[msg_ofs], &out_size, decomp_flags);
        in_ofs += in_size;
        msg_ofs += out_size;
        if (status == TINFL_STATUS_BAD_PARAM || status == TINFL_STATUS_ADLER32_MISMATCH || status == TINFL_STATUS_FAILED || status == TINFL_STATUS_HAS_MORE_OUTPUT || status == TINFL_STATUS_FAILED)
          break;
      }
      if (status == TINFL_STATUS_DONE && (flags >> 1) & 1) {
        digitalWrite(WAKEUP_PIN, HIGH);
        free(message);
        message = NULL;
        Serial1.write(0x00);
        free(decomp);  // fin
        Serial.println("Start draw");
        DisplayFrame(IMAGE);
        Serial.println("End draw");
        digitalWrite(EPAPER_PIN, HIGH);
      }
      else if (status != TINFL_STATUS_NEEDS_MORE_INPUT)
        Serial1.write(0xff);
      else
        Serial1.write(0x00);
    }
  }
}
