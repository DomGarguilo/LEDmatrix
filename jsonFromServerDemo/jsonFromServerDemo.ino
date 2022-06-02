#include <HTTPClient.h>
#include <WiFi.h>
#include <FastLED.h>
#include <ArduinoJson.h>
// #include "OTA.h"
#include <credentials.h>

//variables for LED setup
#define NUM_LEDS 256
#define COLOR_ORDER GRB
#define CHIPSET WS2812B
#define BRIGHTNESS 3
#define DATA_PIN 13  // Connected to the data pin of the first LED strip

// Define the array of leds
CRGB leds[NUM_LEDS];
// Define filename. Located in /data sub-directory
const char *path = "https://led-matrix-server.herokuapp.com/data";


// Json package used to read the json from file and return a json object
JsonObject getJSonFromString(DynamicJsonDocument *doc, String getJson) {

    DeserializationError error = deserializeJson(*doc, getJson);
    
    if (error) {
      // if the file didn't open, print an error:
      Serial.print(F("Error parsing JSON "));
      Serial.println(error.c_str());
      
      return doc->to<JsonObject>();
    }

    return doc->as<JsonObject>();
}

void setup() {

  FastLED.addLeds<CHIPSET, DATA_PIN, COLOR_ORDER>(leds, NUM_LEDS).setCorrection(TypicalSMD5050); // Init of the Fastled library
  FastLED.setBrightness(BRIGHTNESS);
  FastLED.clear(true);

  // Initialize serial port
  Serial.begin(115200);
  while (!Serial) continue;

  // setup over-the-air upload
  // setupOTA("TemplateSketch", mySSID, myPASSWORD);

  WiFi.begin(mySSID, myPASSWORD);
  
  Serial.println("Connecting to wifi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.print(".");
  }

  Serial.println("Connected to wifi");

  String getJson;
  
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    http.useHTTP10(true);
    //http.begin("https://led-matrix-server.herokuapp.com/data/");
    http.begin("http://192.168.0.241:5000/data");
    int httpCode = http.GET();

    if (httpCode > 0) {
      Serial.println("GET success");
      getJson = http.getString();
    } else {
      Serial.println("GET Failure");
      Serial.println(httpCode);
    }
    http.end();
  }

  DynamicJsonDocument json (50948);
  JsonObject doc;
  doc = getJSonFromString(&json, getJson);

  int imgNum = doc[F("animationList")].size();
  Serial.print(imgNum);
  Serial.println(F(" images/animations found"));

  // main loop displaying animations
  while (1) {
    for (int i = 0; i < imgNum; i++) { //iterate through list of animations
      Serial.print(F("now playing: "));
      const char* imgName = doc[F("animationList")][i][F("name")].as<char*>();
      Serial.println(imgName);
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
            // ArduinoOTA.handle();
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
