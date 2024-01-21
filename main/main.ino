#include <FastLED.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <FS.h>
#include <SPIFFS.h>
#include "secrets.h"

struct AnimationMetadata {
  String animationID;
  int frameDuration;
  int repeatCount;
  int totalFrames;
  std::vector<String> frameOrder;

  AnimationMetadata(String id = "", int duration = 0, int repeat = 0, int frames = 0)
    : animationID(id), frameDuration(duration), repeatCount(repeat), totalFrames(frames) {}
};


// board setup
#define DATA_PIN 13
#define LENGTH 16
#define NUM_LEDS (LENGTH * LENGTH)
#define COLOR_ORDER GRB
#define CHIPSET WS2812B
#define BRIGHTNESS 3

// other constants
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
void fetchAndStoreFrameData(HTTPClient& http, String frameID) {
  String url = SERVER_BASE_URL;
  url += "frameData/" + frameID;
  http.begin(url);
  int httpCode = http.GET();

  if (httpCode == HTTP_CODE_OK) {
    WiFiClient* stream = http.getStreamPtr();
    int bytesRead = stream->readBytes(frameDataBuffer, SIZE);
    if (bytesRead == SIZE) {
      saveFrameToSPIFFS(frameID);
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
      AnimationMetadata metadata(
        anim["animationID"].as<String>(),
        anim["frameDuration"].as<int>(),
        anim["repeatCount"].as<int>(),
        anim["totalFrames"].as<int>());

      JsonArray order = anim["frameOrder"].as<JsonArray>();
      for (String frameId : order) {
        metadata.frameOrder.push_back(frameId);
      }

      animations[animationCount++] = metadata;
    }
  }
}

void saveFrameToSPIFFS(String frameID) {
  String fileName = "/" + frameID.substring(0, 25) + ".bin";

  File file = SPIFFS.open(fileName, FILE_WRITE, true);
  if (!file) {
    Serial.println("Failed to open file for writing. Filename: " + fileName);
    return;
  }

  file.write(frameDataBuffer, SIZE);
  file.close();
  Serial.println("Frame saved to SPIFFS: " + fileName);
}

void readFrameFromSPIFFS(String frameID) {
  String fileName = "/" + frameID.substring(0, 25) + ".bin";

  File file = SPIFFS.open(fileName, FILE_READ);
  if (!file) {
    Serial.println("Failed to open file for reading. Filename: " + fileName);
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
  for (int row = 0; row < LENGTH; ++row) {
    for (int col = 0; col < LENGTH; ++col) {
      int ledIndex = row * LENGTH + col;
      int bufferIndex;

      // account for serpentine layout of led strips
      if (row % 2 == 0) {
        // Even rows: direct mapping
        bufferIndex = ledIndex * BYTES_PER_PIXEL;
      } else {
        // Odd rows: reverse order
        int reverseCol = LENGTH - 1 - col;
        bufferIndex = (row * LENGTH + reverseCol) * BYTES_PER_PIXEL;
      }

      leds[ledIndex] = CRGB(frameDataBuffer[bufferIndex], frameDataBuffer[bufferIndex + 1], frameDataBuffer[bufferIndex + 2]);
    }
  }
  FastLED.show();
}

// DEBUG METHODS

void connectToWifi() {
  WiFi.begin(SSID, WIFI_PASSWORD);
  Serial.println("Connecting to WiFi...");

  // Wait for connection
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 200) {
    delay(100);
    Serial.print(".");
    attempts++;
  }

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Failed to connect to WiFi. Please check credentials and try again.");
  } else {
    Serial.println("");
    Serial.println("WiFi connected successfully.");
    Serial.print("IP Address of this device: ");
    Serial.println(WiFi.localIP());
  }
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
    Serial.print("Frame Order: ");
    for (const String& frameId : animations[i].frameOrder) {
      Serial.print(frameId + " ");
    }
    Serial.println("\n-----");
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
    Serial.print(" ");                      // Space between bytes
  }
  Serial.println("\n-------------------");
}

void listSPIFFSFiles() {
  File root = SPIFFS.open("/");
  File file = root.openNextFile();
  while (file) {
    Serial.println(file.name());
    file = root.openNextFile();
  }
}

void writeTestFile() {
  File file = SPIFFS.open("/testfile.txt", FILE_WRITE, true);
  if (!file) {
    Serial.println("There was an error opening the file for writing");
    return;
  }

  if (file.print("This is a test file.")) {
    Serial.println("File was written successfully");
  } else {
    Serial.println("File write failed");
  }

  file.close();
}

void setup() {

  Serial.begin(115200);

  FastLED.addLeds<CHIPSET, DATA_PIN, COLOR_ORDER>(leds, NUM_LEDS).setCorrection(TypicalSMD5050);  // Init of the Fastled library
  FastLED.setBrightness(BRIGHTNESS);
  FastLED.clear(true);

  connectToWifi();

  if (!SPIFFS.begin(true)) {
    Serial.println("An Error has occurred while mounting SPIFFS");
    return;
  }

  //SPIFFS.format();

  writeTestFile();

  listSPIFFSFiles();

  HTTPClient http;

  fetchAndInitMetadata(http);
  printAnimationMetadata();

  // retrieve each frame from the server and write it to a file
  for (int i = 0; i < animationCount; i++) {
    Serial.println("Saving to spiffs: " + animations[i].animationID);
    for (int j = 0; j < animations[i].totalFrames; j++) {
      fetchAndStoreFrameData(http, animations[i].frameOrder[j]);
    }
  }
}

void loop() {
  for (int i = 0; i < animationCount; i++) {
    Serial.println("Displaying Animation: " + animations[i].animationID);
    for (int j = 0; j < animations[i].totalFrames; j++) {
      String currentFrameID = animations[i].frameOrder[j];
      Serial.println("Displaying Frame ID: " + currentFrameID);

      readFrameFromSPIFFS(currentFrameID);
      parseAndDisplayFrame();
      delay(animations[i].frameDuration * 3);
    }
  }
}
