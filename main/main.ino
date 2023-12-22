#include <FastLED.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <FS.h>
#include <SPIFFS.h>
#include "secrets.h"

// SCRUB WIFI CREDS BEFORE PUSHING

struct AnimationMetadata {
  String animationID;
  int frameDuration;
  int repeatCount;
  int totalFrames;

  AnimationMetadata(String id = "", int duration = 0, int repeat = 0, int frames = 0)
    : animationID(id), frameDuration(duration), repeatCount(repeat), totalFrames(frames) {}
};

#define DATA_PIN 22
#define NUM_LEDS 9
#define BYTES_PER_PIXEL 3
#define MAX_ANIMATIONS 10
#define SIZE (NUM_LEDS * BYTES_PER_PIXEL)

AnimationMetadata animations[MAX_ANIMATIONS];
int animationCount = 0;
uint8_t frameDataBuffer[SIZE];

CRGB leds[NUM_LEDS];

// Fetches metadata and uses it to initialize an AnimationMetadata object
void fetchAndInitMetadata(HTTPClient& http) {
  String url = SERVER_BASE_URL;
  url += "metadata";
  http.begin(url);
  int httpCode = http.GET();

  if (httpCode == HTTP_CODE_OK) {
    WiFiClient& stream = http.getStream();
    deserializeAndStoreMetadata(stream);
  } else {
    Serial.print("Metadata fetch failed, HTTP code: ");
    Serial.println(httpCode);
  }

  http.end();
}

// Fetches frame data for a given animation ID and stores it in SPIFFS
void fetchAndStoreFrameData(String animationID, int frameNumber, HTTPClient& http) {
  String url = SERVER_BASE_URL;
  url += "frameData/" + animationID + "/" + frameNumber;
  http.begin(url);
  int httpCode = http.GET();

  if (httpCode == HTTP_CODE_OK) {
    WiFiClient* stream = http.getStreamPtr();
    int bytesRead = stream->readBytes(frameDataBuffer, SIZE);
    if (bytesRead == SIZE) {
      printFrameData();
      saveFrameToSPIFFS(animationID, frameNumber);
    } else {
      Serial.print("Unexpected frame size: ");
      Serial.println(bytesRead);
    }

  } else {
    Serial.print("Frame data fetch failed, HTTP code: ");
    Serial.println(httpCode);
  }

  http.end();
}

void deserializeAndStoreMetadata(WiFiClient& stream) {
  DynamicJsonDocument doc(4096);
  DeserializationError error = deserializeJson(doc, stream);

  if (error) {
    Serial.print("deserializeJson() failed: ");
    Serial.println(error.c_str());
    return;
  }

  JsonArray animationsArray = doc.as<JsonArray>();
  animationCount = 0;
  for (JsonObject anim : animationsArray) {
    if (animationCount < MAX_ANIMATIONS) {
      animations[animationCount++] = AnimationMetadata(
        anim["animationID"].as<String>(),
        anim["frameDuration"].as<int>(),
        anim["repeatCount"].as<int>(),
        anim["totalFrames"].as<int>());
    }
  }
}

void saveFrameToSPIFFS(String animationID, int frameNumber) {
  String fileName = "/" + animationID + "_" + String(frameNumber) + ".bin";

  File file = SPIFFS.open(fileName, FILE_WRITE);
  if (!file) {
    Serial.println("Failed to open file for writing");
    return;
  }

  file.write(frameDataBuffer, SIZE);
  file.close();
  Serial.println("Frame saved to SPIFFS: " + fileName);
}

void readFrameFromSPIFFS(String animationID, int frameNumber) {
  String fileName = "/" + animationID + "_" + String(frameNumber) + ".bin";

  File file = SPIFFS.open(fileName, FILE_READ);
  if (!file) {
    Serial.println("Failed to open file for reading");
    return;
  }

  size_t bytesRead = file.read(frameDataBuffer, SIZE);
  if (bytesRead != SIZE) {
    Serial.println("Failed to read full frame");
  }
  file.close();
  Serial.println("Frame read from SPIFFS: " + fileName);
}

void parseAndDisplayFrame() {
  for (int i = 0; i < NUM_LEDS; ++i) {
    int index = i * BYTES_PER_PIXEL;
    leds[i] = CRGB(frameDataBuffer[index], frameDataBuffer[index + 1], frameDataBuffer[index + 2]);
  }
  FastLED.show();
}

void printAnimationMetadata() {
  for (int i = 0; i < animationCount; ++i) {
    Serial.print("Animation ID: ");
    Serial.println(animations[i].animationID);
    Serial.print("Frame Duration: ");
    Serial.println(animations[i].frameDuration);
    Serial.print("Repeat Count: ");
    Serial.println(animations[i].repeatCount);
    Serial.print("Total Frames: ");
    Serial.println(animations[i].totalFrames);
    Serial.println("-----");
  }
}

void printFrameData() {
  Serial.println("Frame Data:");
  for (size_t i = 0; i < SIZE; i++) {
    if (i > 0 && i % BYTES_PER_PIXEL == 0) {
      Serial.println();  // New line every 3 bytes (1 RGB LED)
    }
    Serial.print("0x");
    if (frameDataBuffer[i] < 0x10) {  // Print leading zero for single digit hex numbers
      Serial.print("0");
    }
    Serial.print(frameDataBuffer[i], HEX);  // Print byte in HEX
    Serial.print(" ");                // Space between bytes
  }
  Serial.println("\n-------------------");
}

void connectToWifi() {
  WiFi.begin(SSID, WIFI_PASSWORD);
  Serial.println("Connecting to WiFi...");

  // Wait for connection
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {  // 20 attempts
    delay(500);
    Serial.print(".");
    attempts++;
  }

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Failed to connect to WiFi. Please check credentials and try again.");
  } else {
    Serial.println("");
    Serial.println("WiFi connected successfully.");
    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP());
  }
}

void setup() {

  Serial.begin(115200);

  FastLED.addLeds<NEOPIXEL, DATA_PIN>(leds, NUM_LEDS);  // GRB ordering is assumed

  connectToWifi();

  if (!SPIFFS.begin(true)) {
    Serial.println("An Error has occurred while mounting SPIFFS");
    return;
  }

  HTTPClient http;

  fetchAndInitMetadata(http);
  printAnimationMetadata();

  // retrieve each frame from the server and write it to a file
  for (int i = 0; i < animationCount; i++) {
    Serial.println("Saving to spiffs: " + animations[i].animationID);
    for (int j = 0; j < animations[i].totalFrames; j++) {
      fetchAndStoreFrameData(animations[i].animationID, j, http);
    }
  }
}

void loop() {
  for (int i = 0; i < animationCount; i++) {
    Serial.println("Displaying " + animations[i].animationID);
    for (int j = 0; j < animations[i].totalFrames; j++) {
      readFrameFromSPIFFS(animations[i].animationID, j );
      parseAndDisplayFrame();
      printFrameData();
      delay(animations[i].frameDuration * 3);
    }
  }
}
