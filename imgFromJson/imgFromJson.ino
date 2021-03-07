#include <FastLED.h>
#include <ArduinoJson.h>
#include <SPIFFS.h>

//variables for LED setup
#define NUM_LEDS 256
#define COLOR_ORDER GRB
#define CHIPSET WS2812B
#define BRIGHTNESS 35
#define DATA_PIN 13  // Connected to the data pin of the first LED strip

// Define the array of leds
CRGB leds[NUM_LEDS];
const char *fileName = "/data.json";
File myFile;
//JsonObject doc;

JsonObject getJSonFromFile(DynamicJsonDocument *doc, String fileName, bool forceCleanONJsonError = true ) {
  // open the file for reading:
  myFile = SPIFFS.open(fileName);
  if (myFile) {

    DeserializationError error = deserializeJson(*doc, myFile);
    if (error) {
      // if the file didn't open, print an error:
      Serial.print(F("Error parsing JSON "));
      Serial.println(error.c_str());

      if (forceCleanONJsonError) {
        return doc->to<JsonObject>();
      }
    }

    // close the file:
    myFile.close();

    return doc->as<JsonObject>();
  } else {
    // if the file didn't open, print an error:
    Serial.print(F("Error opening (or file not exists) "));
    Serial.println(fileName);

    Serial.println(F("Empty json created"));
    return doc->to<JsonObject>();
  }

}

bool saveJSonToAFile(DynamicJsonDocument *doc, String fileName) {
  SPIFFS.remove(fileName);

  // open the file. note that only one file can be open at a time,
  // so you have to close this one before opening another.
  Serial.println(F("Open file in write mode"));
  myFile = SPIFFS.open(fileName, FILE_WRITE);
  if (myFile) {
    Serial.print(F("fileName --> "));
    Serial.println(fileName);

    Serial.print(F("Start write..."));

    serializeJson(*doc, myFile);

    Serial.print(F("..."));
    // close the file:
    myFile.close();
    Serial.println(F("done."));

    return true;
  } else {
    // if the file didn't open, print an error:
    Serial.print(F("Error opening "));
    Serial.println(fileName);

    return false;
  }
}

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

  DynamicJsonDocument json (50948);
  JsonObject doc;
  doc = getJSonFromFile(&json, fileName);


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

}
