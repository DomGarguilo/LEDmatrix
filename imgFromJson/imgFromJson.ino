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

void setup() {

  FastLED.addLeds<CHIPSET, DATA_PIN, COLOR_ORDER>(leds, NUM_LEDS).setCorrection(TypicalSMD5050); // Init of the Fastled library
  FastLED.setBrightness(BRIGHTNESS);
  FastLED.clear(true);

  // Initialize serial port
  Serial.begin(115200);
  while (!Serial) continue;

  if (!SPIFFS.begin(true)) {
    Serial.println(F("An Error has occurred while mounting SPIFFS"));
    return;
  }

  File file = SPIFFS.open(F("/data.json"));
  if (!file) {
    Serial.println(F("Failed to open file for reading"));
    while (1);
  }

  DynamicJsonDocument json (50948);
  DeserializationError error = deserializeJson(json, file);
  file.close();

  // Test if parsing succeeds.
  if (error) {
    Serial.print(F("deserializeJson() failed: "));
    Serial.println(error.c_str());
    return;
  }
  JsonObject doc = json.as<JsonObject>();

  int imgNum = doc[F("animationList")].size();
  Serial.print(imgNum);
  Serial.println(F(" images/animations found"));

  // main loop displaying animations
  while (1) {
    for (int i = 0; i < imgNum; i++) { //iterate through list of animations
      //Serial.print(F("now playing: "));
      const char* imgName = doc[F("animationList")][i][F("name")].as<char*>();
      //Serial.println(imgName);
      int frameCount = doc[F("animationList")][i][F("frames")].size();
      //Serial.println(frameCount);
      int repeatCount = doc[F("animationList")][i][F("repeatCount")].as<int>();
      //Serial.println(repeatCount);
      long frameDuration = doc[F("animationList")][i][F("frameDuration")].as<long>();
      //Serial.println(frameDuration);
      for (int l = 0; l < repeatCount; l++) { //number of times set to repeat animation
        //Serial.print("repitition:");
        //Serial.println(l);
        for (int k = 0; k < frameCount; k++) { //iterates through animation frames
          //Serial.print("frame:");
          //Serial.println(k);
          for (int j = 0; j < 256; j++) { //iterate through pixels of images
            const char* temp = doc[F("animationList")][i][F("frames")][k][j].as<char*>();
            long longVal = strtol(temp, NULL, 16);
            leds[j] = longVal;
          }
          FastLED.show();
          delay(frameDuration); // sets the duration of each frame;
        }
      }
    }
  }
}

void loop() {
  // not reached
}
