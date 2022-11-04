#include "epd7in5b.h"


extern Epd epd;

int CharToInt(char* number);
int ConstCharToInt(const char* number);
int GetTopicNumber(char* text, char* sep);
void DisplayFrame(unsigned char* image_data);
void DisplayQuarterFrame(unsigned char* image_data);

