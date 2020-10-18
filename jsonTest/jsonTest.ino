#include <FastLED.h>
#include <ArduinoJson.h>
#include <SPIFFS.h>

//variables for LED setup
#define NUM_LEDS 256
#define COLOR_ORDER GRB
#define CHIPSET WS2812B
#define BRIGHTNESS 2
#define DATA_PIN 13  // Connected to the data pin of the first LED strip

// Define the array of leds
CRGB leds[NUM_LEDS];

long arr[256];

void setup() {
  // Initialize serial port
  Serial.begin(115200);
  while (!Serial) continue;

  if (!SPIFFS.begin(true)) {
    Serial.println("An Error has occurred while mounting SPIFFS");
    return;
  }
  File file2 = SPIFFS.open("/data.json");


  if (!file2) {
    Serial.println("Failed to open file for reading");
    return;
  }

  String str = file2.readString();
  size_t size = file2.size();
  DynamicJsonDocument doc(7000);
  Serial.print("size ");
  Serial.println(size);
  file2.close();
  char chars[size];
  str.toCharArray(chars, str.length() + 1);

  DeserializationError error = deserializeJson(doc, chars);

  // Test if parsing succeeds.
  if (error) {
    Serial.print(F("deserializeJson() failed: "));
    Serial.println(error.c_str());
    return;
  }

  const char* picName = doc["name"];
  Serial.print("Loaded name ");
  Serial.println(picName);

  
  for (int i = 0; i < 256; i++) {
    String temp = doc["cars"][i].as<String>();
    char c[temp.length() + 1];
    temp.toCharArray(c, temp.length() + 1);
    long longVal = strtol(c, NULL, 16);
    arr[i] = longVal;
  }

  FastLED.addLeds<CHIPSET, DATA_PIN, COLOR_ORDER>(leds, NUM_LEDS).setCorrection(TypicalSMD5050); // Init of the Fastled library
  FastLED.setBrightness(BRIGHTNESS);
  FastLED.clear(true);
  
}

void loop() {
  // put your main code here, to run repeatedly:
  frame(arr);
  delay(500);
  FastLED.clear(true);
  delay(500);
}

void frame(long arr[]) {
  for (int i = 0; i < NUM_LEDS; i++) {
    leds[i] = arr[i];
  }
  FastLED.show();
}
