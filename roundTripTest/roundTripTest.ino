#include <StreamUtils.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <FastLED.h>
#include <ArduinoJson.h>
#include <credentials.h>
#include "SPIFFS.h"

//variables for LED setup
#define NUM_LEDS 256
#define COLOR_ORDER GRB
#define CHIPSET WS2812B
#define BRIGHTNESS 3
#define DATA_PIN 13  // Connected to the data pin of the first LED strip

// Define the array of leds
CRGB leds[NUM_LEDS];
//const char *basePath = "https://led-matrix-server.herokuapp.com/data";
const char *basePath = "http://192.168.0.241:5000";
//String basePath = F("http://192.168.0.241:5000");
const String jsonSuffix = F(".json");
const String slash = F("/");
JsonArray orderArr;
const char* destination[7];

// seems like the json string or the json doc will fit in mem but not both
// need to get the json string, loop through, write each animation to its own file then
// store the file paths in an array which will be read from mem as needed then removed from mem


// Json package used to read the json from string and return a json object
JsonObject getJSonFromString(DynamicJsonDocument *doc, String getJson) {

  DeserializationError error = deserializeJson(*doc, getJson);

  if (error) {
    // if the file didn't open, print an error:
    Serial.print(F("Error parsing JSON "));
    Serial.println(error.c_str());
  }

  return doc->as<JsonObject>();
}

DynamicJsonDocument getRequest(String path) {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    http.useHTTP10(true);
    http.begin(basePath + path);
    int httpCode = http.GET();

    if (httpCode > 0) {
      Serial.print(F("GET success on path: "));
      Serial.println(path);
      DynamicJsonDocument doc (24000);
      Stream& response = http.getStream();
      //ReadBufferingStream bufferedFile(file, 64);
      //deserializeJson(doc, bufferedFile);
      DeserializationError err = deserializeJson(doc, response);
      if (err) {
        Serial.print(F("deserializeJson() failed: "));
        Serial.println(err.c_str());
      }
      doc.shrinkToFit();
      http.end();
      return doc;
    } else {
      Serial.print(httpCode);
      Serial.print(F(" - GET Failure on path: "));
      Serial.println(path);
    }
    http.end();
  }
}

// initialize all files needed for animations
void initFiles() {
  String dataFilePrefix = F("/data/");

  for (JsonVariant v : orderArr) {
    String currentName = v.as<String>();

    // create file
    String path = slash + currentName + jsonSuffix;
    Serial.print(F("Creating file: "));
    Serial.println(path);
    File file = SPIFFS.open(path, FILE_WRITE);
    if (!file) {
      Serial.println(F("There was an error opening the file for writing"));
      return;
    }
    //TODO: LOOK INTO READ FROM HTTP TO JSON
    //TODO: LOOK INTO WRITING FROM JSON STRAIGHT TO FILE
    // get data for current animation from server
    //String currentJson = getRequest(dataFilePrefix + currentName);
    //    Stream& response = getRequest(dataFilePrefix + currentName);
    //    DynamicJsonDocument doc (24000);
    //    DeserializationError err = deserializeJson(doc, response);
    //    if (err) {
    //      Serial.print(F("deserializeJson() failed: "));
    //      Serial.println(err.c_str());
    //    }
    //    doc.shrinkToFit();
    //    Serial.println(doc.memoryUsage());

    // write data to file
    DynamicJsonDocument doc = getRequest(dataFilePrefix + currentName);
    serializeJson(doc, file);

    Serial.println(F("File was written"));

    //    // write data to file
    //    if (file.print(currentJson)) {
    //      Serial.println(F("File was written"));
    //    } else {
    //      Serial.println(F("File write failed"));
    //    }
    file.close();
  }
}

DynamicJsonDocument readFile(String filename) {
  filename = slash + filename + jsonSuffix;
  File file = SPIFFS.open(filename, FILE_READ);

  if (!file) {
    Serial.println(F("Failed to open file for reading"));
  }
  Serial.print(F("Now reading file: "));
  Serial.println(filename);

  //  String fileData;
  //  //  while (file.available()) {
  //  //    fileData += char(file.read());
  //  //  }
  //Serial.println(file.readString());
  DynamicJsonDocument doc (44000);
  //ReadBufferingStream bufferedFile(file, 64);
  //deserializeJson(doc, bufferedFile);
  DeserializationError err = deserializeJson(doc, file);
  if (err) {
    Serial.print(F("deserializeJson() failed: "));
    Serial.println(err.c_str());
  }
  file.close();
  doc.shrinkToFit();

  //return doc.to<JsonObject>();
  return doc;
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

  //  Serial.println(F("formatting SPIFFS"));
  //  if (!SPIFFS.format()) {
  //    Serial.println(F("An Error has occurred while formatting SPIFFS"));
  //    return;
  //  }

  WiFi.begin(mySSID, myPASSWORD);

  Serial.println(F("Connecting to wifi"));
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.print(".");
  }

  Serial.println(F("Connected to wifi"));

  // TODO: replace this with http stream to json
  //String order = getRequest(F("/order"));
  // Stream response = getRequest(F("/order"));
  Serial.println("about to try to deserialize from stream");
  // Serial.println(response);
  //  DynamicJsonDocument doc (2048);
  //  DeserializationError error = deserializeJson(doc, response);
  //  if (error) {
  //    // if the file didn't open, print an error:
  //    Serial.print(F("Error parsing JSON "));
  //    Serial.println(error.c_str());
  //  }
  //  doc.shrinkToFit();
  DynamicJsonDocument doc = getRequest(F("/order"));
  JsonObject json = doc.as<JsonObject>();
  //JsonObject json = getJSonFromString(&json, order);
  // TODO: convert this to regular array. built in arduinojson api
  orderArr = json[F("order")].as<JsonArray>();
  //const char * destination[orderArr.size()];
  int count = 0;
  Serial.println("HERE");
  for (JsonVariant elem : orderArr) {
    Serial.println(elem.as<char *>());
    destination[count] = elem.as<char *>();
    count++;
  }
  //copyArray(orderArr, destination);
  Serial.print(orderArr.size());
  Serial.println(F(" animations found in order array"));
  for (JsonVariant v : orderArr) {
    Serial.println(v.as<String>());
  }
  //int destLength = sizeof(destination) / sizeof(char);
  int destLength = orderArr.size();
  Serial.print(sizeof(destLength));
  Serial.println(F(" animations found in NEW ARRAY"));
  for (int i = 0; i < destLength; i++) {
    String current = String(destination[i]);
    Serial.println("index: " + String(i) + " value: " + current);
  }
  initFiles();

}

void printInfo(String s) {
  DynamicJsonDocument doc = readFile(s);
  //JsonObject dataJson = readFile(v.as<String>());
  Serial.print("In printInfo(), doc returned with size of: ");
  Serial.println(measureJson(doc));
  for (JsonPair keyValue : doc.as<JsonObject>()) {
    Serial.println(keyValue.key().c_str());
  }
  Serial.println("Name: " + doc.as<JsonObject>()[F("name")].as<String>());
  delay(1000);
}

// main loop displaying animations
void loop() {
  // use this array to open the files by name, read the data then unallocate both
  Serial.println("DEBUG: in main loop now");
  //  Serial.print(orderArr.size());
  //  Serial.println(F(" animations found in order array"));
  //int destLength = sizeof(destination) / sizeof(char);
  //Serial.print(sizeof(destLength));
  Serial.println(F(" animations found in NEW ARRAY. Printing contents of Destination array"));
  for (int i = 0; i < 7; i++) {
    String currentName = String(destination[i]);
    Serial.print("About to call print with index DEFINE LARGE ARRAY AT BEGGINIGN THEN JUST STORE LENGTH AS SEPARATE GLOBAL VAR ");
    Serial.print(i);
    Serial.println(currentName);
    
    //printInfo(String(destination[i]));
  }
  //  for (JsonVariant v : orderArr) {
  //    Serial.println("DEBUG: about to call printInfo() for " + v.as<String>());
  //    //printInfo(v.as<String>());
  //    Serial.println("DEBUG: DONE calling printInfo() for " + v.as<String>());
  //    //    DynamicJsonDocument json (24000);
  //    //    JsonObject dataJson = getJSonFromString(&json, dataString);
  //    //    Serial.println(dataJson[F("name")].as<String>());
  //  }
  delay(10000);
  //  while (1) {
  //    for (int i = 0; i < imgNum; i++) { //iterate through list of animations
  //      Serial.print(F("now playing: "));
  //      const char* imgName = doc[F("animationList")][i][F("name")].as<char*>();
  //      Serial.println(imgName);
  //      int frameCount = doc[F("animationList")][i][F("frames")].size();
  //      //Serial.println(frameCount);
  //      int repeatCount = doc[F("animationList")][i][F("repeatCount")].as<int>();
  //      //Serial.println(repeatCount);
  //      long frameDuration = doc[F("animationList")][i][F("frameDuration")].as<long>();
  //      //Serial.println(frameDuration);
  //      for (int l = 0; l < repeatCount; l++) { //number of times set to repeat animation
  //        //Serial.print("repitition:");
  //        //Serial.println(l);
  //        for (int k = 0; k < frameCount; k++) { //iterates through animation frames
  //          //Serial.print("frame:");
  //          //Serial.println(k);
  //          for (int j = 0; j < 256; j++) { //iterate through pixels of images
  //            const char* temp = doc[F("animationList")][i][F("frames")][k][j].as<char*>();
  //            long longVal = strtol(temp, NULL, 16);
  //            leds[j] = longVal;
  //            // ArduinoOTA.handle();
  //          }
  //          FastLED.show();
  //          delay(frameDuration); // sets the duration of each frame;
  //        }
  //      }
  //    }
  //  }
}
