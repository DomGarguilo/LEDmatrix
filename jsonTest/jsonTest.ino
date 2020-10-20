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
  // Initialize serial port
  Serial.begin(115200);
  while (!Serial) continue;

  if (!SPIFFS.begin(true)) {
    Serial.println(F("An Error has occurred while mounting SPIFFS"));
    return;
  }

  File file = SPIFFS.open(F("/data1.json"));
  if (!file) {
    Serial.println(F("Failed to open file for reading"));
    while (1);
  }

  DynamicJsonDocument json (50948);
  DeserializationError error = deserializeJson(json, file);


  // Test if parsing succeeds.
  if (error) {
    Serial.print(F("deserializeJson() failed: "));
    Serial.println(error.c_str());
    return;
  }
  JsonObject doc = json.as<JsonObject>();

  int imgNum = doc[F("images")].size();
  Serial.print(imgNum);
  Serial.println(F(" images found"));
  long imgArray[imgNum][256];

  for (int i = 0; i < imgNum; i++) { //iterate through images
    Serial.print(F("now reading image: "));
    const char* imgName = doc[F("images")][i][F("name")].as<char*>();
    Serial.println(imgName);
    for (int j = 0; j < 256; j++) { //iterate through image data
      const char* temp = doc[F("images")][i][F("data")][j].as<char*>();
      long longVal = strtol(temp, NULL, 16);
      imgArray[i][j] = longVal;
    }
  }

  FastLED.addLeds<CHIPSET, DATA_PIN, COLOR_ORDER>(leds, NUM_LEDS).setCorrection(TypicalSMD5050); // Init of the Fastled library
  FastLED.setBrightness(BRIGHTNESS);
  FastLED.clear(true);

  while (1) {
    for (int i = 0; i < imgNum; i++) {
      frame(imgArray[i]);
      delay(1000);
    }
  }
}

void loop() {
  // put your main code here, to run repeatedly:

}

void frame(long arr[]) {
  for (int i = 0; i < NUM_LEDS; i++) {
    leds[i] = arr[i];
  }
  FastLED.show();
}
