#include <FastLED.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <FS.h>
#include <SPIFFS.h>
#include <StreamUtils.h>
#include <Update.h>
#include "secrets.h"

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

#define METADATA_URL SERVER_BASE_URL "metadata"
#define METADATA_HASH_URL SERVER_BASE_URL "metadata/hash/"
#define FRAME_DATA_BASE_URL SERVER_BASE_URL "frameData/"
#define FIRMWARE_URL SERVER_BASE_URL "firmware/"

#define METADATA_FILE_NAME "/metadata.json"

#define FIRMWARE_VERSION "0.0.1"

DynamicJsonDocument metadataDoc(4096);
JsonObject jsonMetadata = metadataDoc.to<JsonObject>();

uint8_t frameDataBuffer[SIZE];

CRGB leds[NUM_LEDS];

enum WiFiConnectionState {
  DISCONNECTED,
  CONNECTING,
  CONNECTED
};

WiFiConnectionState wifiState = DISCONNECTED;

unsigned long lastWiFiAttemptMillis = 0;
const unsigned long wifiAttemptInterval = 20 * 1000;
unsigned long lastFirmwareCheckMillis = 0;
const unsigned long firmwareCheckInterval = 60 * 60 * 1000;
unsigned long lastHashCheckMillis = 0;
const unsigned long hashCheckInterval = 5 * 60 * 1000;  // 5 minute interval

unsigned long previousMillis = 0;  // stores last time frame was updated
int currentFrameIndex = 0;         // index of the current frame
int currentAnimationIndex = 0;     // index of the current animation
bool animationLoaded = false;      // flag to check if the animation details are loaded
JsonObject currentAnimation;       // stores the current animation
JsonArray frameOrder;              // stores the frame order of the current animation
int totalFrames;                   // total number of frames in the current animation
int frameDuration;                 // duration of each frame


void checkOrConnectWifi() {
  switch (wifiState) {
    case DISCONNECTED:
      // Start connecting
      WiFi.begin(SSID, WIFI_PASSWORD);
      wifiState = CONNECTING;
      lastWiFiAttemptMillis = millis();
      Serial.println("Attempting to connect to WiFi...");
      break;
    case CONNECTING:
      // Check if connected
      if (WiFi.status() == WL_CONNECTED) {
        wifiState = CONNECTED;
        Serial.println("Connected to WiFi!");
        Serial.print("IP Address: ");
        Serial.println(WiFi.localIP());
      } else if (millis() - lastWiFiAttemptMillis > wifiAttemptInterval) {
        // Retry after interval
        wifiState = DISCONNECTED;
      }
      break;
    case CONNECTED:
      // Check if still connected
      if (WiFi.status() != WL_CONNECTED) {
        wifiState = DISCONNECTED;
        Serial.println("WiFi connection lost. Attempting to reconnect...");
      }
      break;
  }
}

// fetches metadata and initializes the global metadata json
// returns true if the data was successfully fetched
bool fetchAndInitMetadata(HTTPClient& http) {
  const int maxRetries = 3;
  for (int attempt = 0; attempt < maxRetries; attempt++) {
    http.begin(METADATA_URL);
    int httpCode = http.GET();

    if (httpCode == HTTP_CODE_OK) {
      WiFiClient& stream = http.getStream();
      bool success = deserializeAndStoreMetadata(stream);
      http.end();
      return success;
    } else {
      Serial.print("Metadata fetch attempt ");
      Serial.print(attempt + 1);
      Serial.print(" failed, HTTP code: ");
      Serial.println(httpCode);
      http.end();
      delay(1000);  // wait for 1 second before retrying
    }
  }
  return false;
}

// creates the endpoint to fetch the data of the given frame
void constructFrameDataURL(char* url, const char* frameID, int bufferSize) {
  snprintf(url, bufferSize, "%s%s", FRAME_DATA_BASE_URL, frameID);
}

// Fetches frame data for a given frame ID and stores it in SPIFFS
void fetchAndStoreFrameData(HTTPClient& http, const char* frameID) {
  const int maxRetries = 3;
  char filePath[32];  // max filename length is 32
  constructFilePath(filePath, frameID, sizeof(filePath));

  if (SPIFFS.exists(filePath)) {
    Serial.print("File already exists: ");
    Serial.println(filePath);
    return;
  }

  for (int attempt = 0; attempt < maxRetries; attempt++) {
    char url[100];
    constructFrameDataURL(url, frameID, sizeof(url));

    http.begin(url);
    int httpCode = http.GET();

    if (httpCode == HTTP_CODE_OK) {
      WiFiClient* stream = http.getStreamPtr();
      int bytesRead = stream->readBytes(frameDataBuffer, SIZE);
      if (bytesRead == SIZE) {
        saveFrameToSPIFFS(filePath);
        return;
      } else {
        Serial.print("Unexpected frame size: ");
        Serial.println(bytesRead);
      }
    } else {
      Serial.print("Frame data fetch attempt ");
      Serial.print(attempt + 1);
      Serial.print(" failed, HTTP code: ");
      Serial.println(httpCode);
    }

    http.end();
    delay(1000);
  }
  Serial.println("Failed to fetch frame data after retries.");
}

// illuminate the LEDs in a loading pattern depending on the progress of the firmware upgrade
void updateProgress(size_t prg, size_t sz) {
  uint8_t progressPercent = (prg * 100) / sz;
  Serial.printf("%u%% ", progressPercent);

  uint16_t numLedsToLight = (progressPercent * NUM_LEDS) / 100;

  // Light up the LEDs accordingly
  for (int i = 0; i < NUM_LEDS; i++) {
    if (i < numLedsToLight) {
      leds[i] = CRGB::Green;
    } else {
      leds[i] = CRGB::Black;
    }
  }

  FastLED.show();
}

// uses the given firmware version to check if there is a firmware update available
void checkOrUpdateFirmware(HTTPClient& http) {
  char versionCheckURL[256];
  snprintf(versionCheckURL, sizeof(versionCheckURL), "%s%s", FIRMWARE_URL, FIRMWARE_VERSION);

  Serial.print("Checking against version: ");
  Serial.println(FIRMWARE_VERSION);

  http.begin(versionCheckURL);
  int httpCode = http.GET();

  if (httpCode == 200) {
    Serial.println("Firmware update available!");

    Update.onProgress(updateProgress);  // Set the progress callback

    size_t contentLength = http.getSize();
    bool canBegin = Update.begin(contentLength);

    if (canBegin) {
      Serial.println("Starting firmware update");
      WiFiClient* client = http.getStreamPtr();
      size_t written = Update.writeStream(*client);

      if (written == contentLength) {
        Serial.println("Update successfully completed. Rebooting...");
        Update.end();
        ESP.restart();
      } else {
        Serial.println("Update failed.");
        Update.abort();
      }
    } else {
      Serial.println("Not enough space to begin firmware update");
    }
  } else if (httpCode == 204) {
    Serial.println("Firmware is up to date");
  } else if (httpCode == 501) {
    Serial.println("No firmware available on server.");
  } else if (httpCode == 502) {
    Serial.println("Error reading firmware file on server.");
  } else {
    Serial.print("Failed to check firmware version, HTTP code: ");
    Serial.println(httpCode);
  }
  http.end();
}

void saveMetadataToFile() {
  File file = SPIFFS.open(METADATA_FILE_NAME, FILE_WRITE);
  if (!file) {
    Serial.println("Failed to open metadata file for writing");
    return;
  }

  WriteBufferingStream bufferedFile(file, 64);

  size_t metadataDocSize = measureJson(metadataDoc);
  size_t writtenLength = serializeJson(metadataDoc, bufferedFile);

  bufferedFile.flush();
  file.close();

  if (metadataDocSize == writtenLength) {
    Serial.println("Metadata saved to file. Saw expected length written.");
  } else {
    Serial.println("Did not see expected length written to metadata file.");
  }
}

// fetch new metadata, cleanup files, then fetch new frame data
bool fetchMetadataAndFrames(HTTPClient& http) {
  if (!fetchAndInitMetadata(http)) {
    Serial.println("Failed to fetch metadata.");
    return false;
  }

  cleanupUnusedFiles();

  // fetch frame data for each frame in the metadata
  for (JsonObject animation : jsonMetadata["metadata"].as<JsonArray>()) {
    const char* animationID = animation["animationID"].as<const char*>();
    Serial.print("Saving to SPIFFS: ");
    Serial.println(animationID);
    JsonArray frameOrder = animation["frameOrder"].as<JsonArray>();
    for (const char* frameID : frameOrder) {
      fetchAndStoreFrameData(http, frameID);
    }
  }
  return true;
}

bool loadMetadataFromFile() {
  File file = SPIFFS.open(METADATA_FILE_NAME, FILE_READ);
  if (!file) {
    Serial.println("Failed to open metadata file");
    return false;
  }

  ReadBufferingStream bufferingStream(file, 64);
  DeserializationError error = deserializeJson(metadataDoc, bufferingStream);
  file.close();

  if (error) {
    Serial.print("deserialization of metadata from file failed: ");
    Serial.println(error.c_str());
    return false;
  } else {
    Serial.println("Successfully loaded metadata from save file.");
  }

  return true;
}

// deserialize the metadata json from the stream and store it in file
bool deserializeAndStoreMetadata(WiFiClient& stream) {
  ReadBufferingStream bufferingStream(stream, 64);
  DeserializationError error = deserializeJson(metadataDoc, bufferingStream);

  if (error) {
    Serial.print("deserialization of metadata from wifi stream failed: ");
    Serial.println(error.c_str());
    return false;
  }

  saveMetadataToFile();
  return true;
}

// check if the local metadata hash matches the server's hash
bool doesLocalMetadataMatchServer(HTTPClient& http) {
  // Check if the 'hash' key exists in the JSON object
  if (!jsonMetadata.containsKey("hash") || jsonMetadata["hash"].isNull()) {
    Serial.println("Hash key not found in local metadata.");
    return false;
  }

  char hashCheckURL[256];
  snprintf(hashCheckURL, sizeof(hashCheckURL), "%s%s", METADATA_HASH_URL, jsonMetadata["hash"].as<const char*>());

  http.begin(hashCheckURL);
  int httpCode = http.GET();

  if (httpCode == HTTP_CODE_OK) {
    DynamicJsonDocument hashResponseDoc(1024);
    deserializeJson(hashResponseDoc, http.getStream());
    bool hashesMatch = hashResponseDoc["hashesMatch"].as<bool>();
    http.end();
    return hashesMatch;
  } else {
    Serial.print("Failed to get hash response, HTTP code: ");
    Serial.println(httpCode);
  }

  http.end();
  return false;  // false if request fails or hashes don't match
}


void constructFilePath(char* filePath, const char* frameID, int bufferSize) {
  snprintf(filePath, bufferSize, "/%s.bin", frameID);
}

// delete any frame files that are not refferenced in the current metadata
// this will prevent storage from filling up
void cleanupUnusedFiles() {
  File root = SPIFFS.open("/");
  File file = root.openNextFile();

  while (file) {
    const char* fileName = file.name();
    Serial.print("Assessing file for deletion: ");
    Serial.println(fileName);

    // skip metadata file
    if (strcmp(fileName, METADATA_FILE_NAME + 1) == 0) {
      Serial.println("Found metadata file. Skipping.");
      file.close();
      file = root.openNextFile();
      continue;
    }

    bool isUsed = false;
    for (JsonObject animation : jsonMetadata["metadata"].as<JsonArray>()) {
      JsonArray frameOrder = animation["frameOrder"].as<JsonArray>();
      for (const char* frameID : frameOrder) {
        char expectedFilePath[32];
        constructFilePath(expectedFilePath, frameID, sizeof(expectedFilePath));

        // compare without the leading slash in expectedFilePath
        if (strcmp(fileName, expectedFilePath + 1) == 0) {
          isUsed = true;
          break;
        }
      }
      if (isUsed) break;
    }

    if (!isUsed) {
      // Delete the file using the file name (with the leading slash)
      char fullPath[32];
      snprintf(fullPath, sizeof(fullPath), "/%s", fileName);

      if (SPIFFS.remove(fullPath)) {
        Serial.print("Successfully deleted unused frame file: ");
      } else {
        Serial.print("Failed to delete file: ");
      }
      Serial.println(fullPath);
    }

    file.close();
    file = root.openNextFile();
  }
  root.close();
}

// write the frame data buffer to a file at the given path
void saveFrameToSPIFFS(const char* filePath) {
  File file = SPIFFS.open(filePath, FILE_WRITE, true);
  if (!file) {
    Serial.print("Failed to open file for writing. Filename: ");
    Serial.println(filePath);
    return;
  }

  file.write(frameDataBuffer, SIZE);
  file.close();
  Serial.print("Frame saved to SPIFFS: ");
  Serial.println(filePath);
}

void readFrameFromSPIFFS(const char* frameID) {
  char filePath[32];  // max filename length is 32
  constructFilePath(filePath, frameID, sizeof(filePath));

  File file = SPIFFS.open(filePath, FILE_READ);
  if (!file) {
    Serial.print("Failed to open file for reading. Filename: ");
    Serial.println(filePath);
    return;
  }

  size_t bytesRead = file.read(frameDataBuffer, SIZE);
  if (bytesRead != SIZE) {
    Serial.print("Failed to read full frame from ");
    Serial.println(filePath);
  } else {
    Serial.print("Frame read from SPIFFS: ");
    Serial.println(filePath);
  }
  file.close();
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
  for (JsonObject animation : jsonMetadata["metadata"].as<JsonArray>()) {
    const char* animationID = animation["animationID"].as<const char*>();
    int frameDuration = animation["frameDuration"];
    int repeatCount = animation["repeatCount"];
    int totalFrames = animation["totalFrames"];

    Serial.print("Animation ID: ");
    Serial.println(animationID);
    Serial.print("Frame Duration: ");
    Serial.println(frameDuration);
    Serial.print("Repeat Count: ");
    Serial.println(repeatCount);
    Serial.print("Total Frames: ");
    Serial.println(totalFrames);
    Serial.print("Frame Order: ");

    JsonArray frameOrder = animation["frameOrder"].as<JsonArray>();
    for (const char* frameID : frameOrder) {
      Serial.print(frameID);
      Serial.print(" ");
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

  if (!SPIFFS.begin(true)) {
    Serial.println("An Error has occurred while mounting SPIFFS");
    return;
  }

  //SPIFFS.format();
  writeTestFile();
  listSPIFFSFiles();

  connectToWifi();

  Serial.println("Loading metadata from saved files.");
  if (SPIFFS.exists(METADATA_FILE_NAME)) {
    if (!loadMetadataFromFile()) {
      Serial.println("Failed to load metadata from file");
    }
  } else {
    Serial.println("No saved metadata available.");
  }

  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;

    checkOrUpdateFirmware(http);
    lastFirmwareCheckMillis = millis();

    if (!doesLocalMetadataMatchServer(http)) {  // if metadata is not up to date, fetch new data
      Serial.println("Local metadata is out of date with server. Pulling new data.");
      fetchMetadataAndFrames(http);
    } else {
      Serial.println("Metadata matches server. No need to fetch new data.");
    }
    lastHashCheckMillis = millis();
  } else {
    Serial.println("Not connected to WiFi. Continuing offline.");
  }

  printAnimationMetadata();
}

void loop() {

  if (currentAnimationIndex < jsonMetadata["metadata"].as<JsonArray>().size()) {
    if (!animationLoaded) {  // If this is the first frame of the animation, load the animation
      currentAnimation = jsonMetadata["metadata"].as<JsonArray>()[currentAnimationIndex];
      const char* animationID = currentAnimation["animationID"];
      totalFrames = currentAnimation["totalFrames"];
      frameDuration = currentAnimation["frameDuration"];

      Serial.print("Displaying Animation: ");
      Serial.println(animationID);

      frameOrder = currentAnimation["frameOrder"].as<JsonArray>();
      animationLoaded = true;
    }

    if (millis() - previousMillis >= frameDuration * 3) {
      // Time to show the next frame
      if (currentFrameIndex < frameOrder.size()) {
        // If there are more frames to display
        const char* currentFrameID = frameOrder[currentFrameIndex];

        Serial.print("Displaying Frame ID: ");
        Serial.println(currentFrameID);

        readFrameFromSPIFFS(currentFrameID);
        parseAndDisplayFrame();

        previousMillis = millis();  // save the last time a frame was displayed
        currentFrameIndex++;
      } else {
        // No more frames, move to the next animation
        currentFrameIndex = 0;
        currentAnimationIndex++;
        animationLoaded = false;
      }
    }
  } else {
    // All animations done, reset for the next loop
    currentAnimationIndex = 0;
    animationLoaded = false;
  }

  if (millis() - lastFirmwareCheckMillis >= firmwareCheckInterval) {
    checkOrConnectWifi();

    if (wifiState == CONNECTED) {
      HTTPClient http;
      checkOrUpdateFirmware(http);
      lastFirmwareCheckMillis = millis();
    }
  } else if (millis() - lastHashCheckMillis >= hashCheckInterval) {
    checkOrConnectWifi();

    // Proceed with metadata check if connected to WiFi
    if (wifiState == CONNECTED) {
      HTTPClient http;
      if (!doesLocalMetadataMatchServer(http)) {
        Serial.println("Local metadata is out of date. Updating...");
        fetchMetadataAndFrames(http);

        // Reset animation parameters if new metadata is fetched
        currentAnimationIndex = 0;
        currentFrameIndex = 0;
        animationLoaded = false;
      } else {
        Serial.println("Local metadata is up to date.");
      }
      lastHashCheckMillis = millis();
    }
  } else {
    delay(50);  // small delay to avoid hammering cpu. skip if we fetch new data
  }
}
