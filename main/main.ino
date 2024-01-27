#include <FastLED.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <FS.h>
#include <SPIFFS.h>
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
#define FRAME_DATA_BASE_URL SERVER_BASE_URL "frameData/"
#define METADATA_FILE_NAME "/metadata.json"

DynamicJsonDocument metadataDoc(4096);
JsonArray metadata = metadataDoc.to<JsonArray>();

uint8_t frameDataBuffer[SIZE];

CRGB leds[NUM_LEDS];

// fetches metadata and initializes the global metadata json
// returns true if the data was successfully fetched
bool fetchAndInitMetadata(HTTPClient& http) {
  http.begin(METADATA_URL);
  int httpCode = http.GET();

  if (httpCode == HTTP_CODE_OK) {
    WiFiClient& stream = http.getStream();
    bool success = deserializeAndStoreMetadata(stream);
    http.end();
    return success;
  } else {
    Serial.print("Metadata fetch failed, HTTP code: ");
    Serial.println(httpCode);
    http.end();
    return false;
  }
}

// creates the endpoint to fetch the data of the given frame
void constructFrameDataURL(char* url, const char* frameID, int bufferSize) {
  snprintf(url, bufferSize, "%s%s", FRAME_DATA_BASE_URL, frameID);
}

// Fetches frame data for a given frame ID and stores it in SPIFFS
void fetchAndStoreFrameData(HTTPClient& http, const char* frameID) {
  char filePath[32];  // max filename length is 32
  constructFilePath(filePath, frameID, sizeof(filePath));

  // skip fetching and creating file if the file already exists
  if (SPIFFS.exists(filePath)) {
    Serial.print("File already exists: ");
    Serial.println(filePath);
    return;
  }

  char url[100];
  constructFrameDataURL(url, frameID, sizeof(url));

  http.begin(url);
  int httpCode = http.GET();

  if (httpCode == HTTP_CODE_OK) {
    WiFiClient* stream = http.getStreamPtr();
    int bytesRead = stream->readBytes(frameDataBuffer, SIZE);
    if (bytesRead == SIZE) {
      saveFrameToSPIFFS(filePath);
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

void saveMetadataToFile() {
  File file = SPIFFS.open(METADATA_FILE_NAME, FILE_WRITE);
  if (!file) {
    Serial.println("Failed to open metadata file for writing");
    return;
  }
  size_t metadataDocSize = measureJson(metadataDoc);
  size_t writtenLength = serializeJson(metadataDoc, file);
  file.close();

  if (metadataDocSize == writtenLength) {
    Serial.println("Metadata saved to file. Saw expected length written.");
  } else {
    Serial.println("Did not see expected length written to metadata file.");
  }
}

bool loadMetadataFromFile() {
  File file = SPIFFS.open(METADATA_FILE_NAME, FILE_READ);
  if (!file) {
    Serial.println("Failed to open metadata file");
    return false;
  }

  DeserializationError error = deserializeJson(metadataDoc, file);
  file.close();

  if (error) {
    Serial.print("deserializeJson() failed: ");
    Serial.println(error.c_str());
    return false;
  } else {
    Serial.println("Successfully loaded metadata from save file.");
  }

  return true;
}

// deserialize the metadata json from the stream and store it in file
bool deserializeAndStoreMetadata(WiFiClient& stream) {
  DeserializationError error = deserializeJson(metadataDoc, stream);

  if (error) {
    Serial.print("deserializeJson() failed: ");
    Serial.println(error.c_str());
    return false;
  }

  saveMetadataToFile();
  return true;
}

void constructFilePath(char* filePath, const char* frameID, int bufferSize) {
  // Max filename length of 31 characters (including ".bin") plus mandtadory null terminator.
  const int maxFrameIDLength = 31 - 4 - 1;
  char trimmedFrameID[maxFrameIDLength + 1];  // +1 for null terminator

  // Copy only the first part of the frameID to trimmedFrameID
  strncpy(trimmedFrameID, frameID, maxFrameIDLength);
  trimmedFrameID[maxFrameIDLength] = '\0';  // Ensuring null termination

  // Construct the file path
  snprintf(filePath, bufferSize, "/%s.bin", trimmedFrameID);
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
  for (JsonObject animation : metadata) {
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
  //writeTestFile();
  //listSPIFFSFiles();

  connectToWifi();
  HTTPClient http;

  printAnimationMetadata();

  bool fetchedNewData = false;
  if (WiFi.status() == WL_CONNECTED) {
    fetchedNewData = fetchAndInitMetadata(http);
    if (fetchedNewData) {
      // successfully fetched new metadata. now fetch and store frame data
      printAnimationMetadata();
      for (JsonObject animation : metadata) {
        const char* animationID = animation["animationID"].as<const char*>();
        Serial.print("Saving to SPIFFS: ");
        Serial.println(animationID);
        JsonArray frameOrder = animation["frameOrder"].as<JsonArray>();
        for (const char* frameID : frameOrder) {
          fetchAndStoreFrameData(http, frameID);
        }
      }
    } else {
      Serial.println("Could not fetch new metadata from server.");
    }
  } else {
    Serial.println("Couldn't connect to WiFi.");
  }

  // If failed to fetch new data, use saved metadata
  if (!fetchedNewData) {
    Serial.println("Loading animations from saved files (hopefully)");
    if (SPIFFS.exists(METADATA_FILE_NAME)) {
      loadMetadataFromFile();
    } else {
      Serial.println("No saved metadata available.");
      // Handle the case where no saved data is available
      // maybe restart the esp32 or display error frame
    }
  }
}

void loop() {
  for (JsonObject animation : metadata) {
    const char* animationID = animation["animationID"].as<const char*>();
    int totalFrames = animation["totalFrames"];
    int frameDuration = animation["frameDuration"];

    Serial.print("Displaying Animation: ");
    Serial.println(animationID);
    JsonArray frameOrder = animation["frameOrder"].as<JsonArray>();
    for (const char* currentFrameID : frameOrder) {
      Serial.print("Displaying Frame ID: ");
      Serial.println(currentFrameID);

      readFrameFromSPIFFS(currentFrameID);
      parseAndDisplayFrame();
      delay(frameDuration * 3);
    }
  }
}
