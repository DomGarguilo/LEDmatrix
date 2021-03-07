#include "FastLED.h"  // Fastled library to control the LEDs
#include "SPIFFS.h"   // file system library

#define NUM_LEDS 256
#define COLOR_ORDER GRB
#define CHIPSET WS2812B
#define BRIGHTNESS 2
#define DATA_PIN 13  // Connected to the data pin of the first LED strip

// Define the array of leds
CRGB leds[NUM_LEDS];

// array to hold the long integer values associated with the colors
long DigDug[256];


void setup() {
  // start serial monitor. this is how we can see prints in the code happening on the ESP32
  Serial.begin(115200);
  // open file system
  if (!SPIFFS.begin(true)) {
    Serial.println("An Error has occurred while mounting SPIFFS");
    return;
  }
  //open file
  // this file is located in the /data directory
  File file = SPIFFS.open("/images.txt");
  if (!file) {
    Serial.println("Failed to open file for reading");
    return;
  }
  // fill string from file
  String str = file.readString();
  file.close();
  // filling image array
  // this is old method used to convert the values to hex, now we can just use type hex in the file without need to convert
  for (int i = 0; i < 256; i++) {
    //get substrings of hex values
    int startIndex = i * 10;
    int endIndex = startIndex + 8;
    String temp = str.substring(startIndex, endIndex);
    // convert to char array
    char chars[temp.length()+1];
    temp.toCharArray(chars, temp.length()+1);
    //get numeric value from string
    DigDug[i] = strtol(chars, NULL, 16);
  }
  
  FastLED.addLeds<CHIPSET, DATA_PIN, COLOR_ORDER>(leds, NUM_LEDS).setCorrection(TypicalSMD5050); // Init of the Fastled library
  FastLED.setBrightness(BRIGHTNESS);
  FastLED.clear(true);
}



void loop() {
  // put your main code here, to run repeatedly:
  frame(DigDug);
  delay(500);
  FastLED.clear(true); // clears the led values, turning off the leds
  delay(500);
}

// helper function to loop through and display the color values
void frame(long arr[]) {
  for (int i = 0; i < NUM_LEDS; i++) {
    leds[i] = arr[i];
  }
  FastLED.show();
}
