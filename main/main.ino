// board manager version 3.3.0
#include <FastLED.h>      // v3.10.1
#include <ArduinoJson.h>  // v7.4.2
#include <WiFi.h>
#include <HTTPClient.h>
#include <FS.h>
#include <SPIFFS.h>
#include <StreamUtils.h>  // v1.9.0
#include <Update.h>
#include <WiFiClientSecure.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>

// board setup
#define DATA_PIN 13
#define LENGTH 16
#define NUM_LEDS (LENGTH * LENGTH)
#define COLOR_ORDER GRB
#define CHIPSET WS2812B
#define BRIGHTNESS 20

// other constants
#define BYTES_PER_PIXEL 3
#define MAX_ANIMATIONS 10
#define SIZE (NUM_LEDS * BYTES_PER_PIXEL)

#define METADATA_FILE_NAME "/metadata.json"

#define WIFI_ERROR_FRAME_ID "wifiError"
#define SERVER_ERROR_FRAME_ID "serverError"
#define EMPTY_QUEUE_FRAME_ID "emptyQueue"
#define WIFI_ERROR_FILE_NAME WIFI_ERROR_FRAME_ID ".bin"
#define SERVER_ERROR_FILE_NAME SERVER_ERROR_FRAME_ID ".bin"
#define EMPTY_QUEUE_FILE_NAME EMPTY_QUEUE_FRAME_ID ".bin"

#define FIRMWARE_VERSION "0.0.12"

char* metadataURL;
char* metadataHashURL;
char* frameDataBaseURL;
char* firmwareURL;

DNSServer dnsServer;
WebServer server(80);
Preferences preferences;
const char* apSSID = "ESP32-Setup";
const char* apPassword = "setuppassword";

const IPAddress localIP(4, 3, 2, 1);
const IPAddress gatewayIP(4, 3, 2, 1);
const IPAddress subnetMask(255, 255, 255, 0);

const String localIPURL = "http://4.3.2.1";

JsonDocument metadataDoc;
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
int currentRepeatCount = 0;        // Current count of how many times the animation has been repeated
bool animationLoaded = false;      // flag to check if the animation details are loaded
JsonObject currentAnimation;       // stores the current animation

// used to track the progress of different processes in order to display loading animations
size_t progressStepsCompleted = 0;
size_t totalProgressSteps = 0;


void checkOrConnectWifi() {
  const boolean readOnly = true;
  switch (wifiState) {
    case DISCONNECTED:
      // Start connecting
      preferences.begin("my-app", readOnly);
      if (preferences.isKey("ssid") && preferences.isKey("password")) {
        String ssid = preferences.getString("ssid", "");
        String password = preferences.getString("password", "");
        WiFi.begin(ssid.c_str(), password.c_str());
        wifiState = CONNECTING;
        lastWiFiAttemptMillis = millis();
        Serial.println(F("Attempting to connect to WiFi..."));
      } else {
        Serial.println(F("No stored WiFi credentials."));
        delay(50);
      }
      preferences.end();
      break;
    case CONNECTING:
      // Check if connected
      if (WiFi.status() == WL_CONNECTED) {
        wifiState = CONNECTED;
        Serial.println(F("Connected to WiFi!"));
        Serial.print(F("IP Address: "));
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
        Serial.println(F("WiFi connection lost. Attempting to reconnect..."));
      }
      break;
  }
}

void setURLsFromPreferences() {
  preferences.begin("my-app", true);
  if (preferences.isKey("serverURL")) {
    String storedServerURL = preferences.getString("serverURL", "");

    const char* metadataPath = "metadata/";
    const char* metadataHashPath = "metadata/hash/";
    const char* frameDataPath = "frameData/";
    const char* firmwarePath = "firmware/";

    metadataURL = new char[storedServerURL.length() + strlen(metadataPath) + 1];
    metadataHashURL = new char[storedServerURL.length() + strlen(metadataHashPath) + 1];
    frameDataBaseURL = new char[storedServerURL.length() + strlen(frameDataPath) + 1];
    firmwareURL = new char[storedServerURL.length() + strlen(firmwarePath) + 1];

    sprintf(metadataURL, "%s%s", storedServerURL.c_str(), metadataPath);
    sprintf(metadataHashURL, "%s%s", storedServerURL.c_str(), metadataHashPath);
    sprintf(frameDataBaseURL, "%s%s", storedServerURL.c_str(), frameDataPath);
    sprintf(firmwareURL, "%s%s", storedServerURL.c_str(), firmwarePath);

    Serial.print(F("Server URL: "));
    Serial.println(storedServerURL);
    Serial.print(F("metadataURL: "));
    Serial.println(metadataURL);
    Serial.print(F("metadataHashURL: "));
    Serial.println(metadataHashURL);
    Serial.print(F("frameDataBaseURL: "));
    Serial.println(frameDataBaseURL);
    Serial.print(F("firmwareURL: "));
    Serial.println(firmwareURL);
  } else {
    Serial.println(F("Failed to find server URL in preferences!"));
    metadataURL = nullptr;
    metadataHashURL = nullptr;
    frameDataBaseURL = nullptr;
    firmwareURL = nullptr;
  }
  preferences.end();
}

// fetches metadata and initializes the global metadata json
// returns true if the data was successfully fetched
bool fetchAndInitMetadata(HTTPClient& http, WiFiClientSecure& client) {
  const int maxRetries = 3;
  for (int attempt = 0; attempt < maxRetries; attempt++) {
    http.begin(client, metadataURL);
    int httpCode = http.GET();

    if (httpCode == HTTP_CODE_OK) {
      WiFiClient& stream = http.getStream();
      ReadBufferingStream bufferingStream(stream, 64);
      DeserializationError error = deserializeJson(metadataDoc, bufferingStream);

      if (error) {
        Serial.print(F("deserialization of metadata from wifi stream failed: "));
        Serial.println(error.c_str());
      } else {
        Serial.println(F("Fetched and deserialized metadata."));
        http.end();
        return true;
      }
    } else {
      Serial.print(F("Metadata fetch attempt "));
      Serial.print(attempt + 1);
      Serial.print(F(" failed, HTTP code: "));
      Serial.println(httpCode);
    }
    http.end();
    delay(1000);  // wait for 1 second before retrying
  }
  displayErrorSymbol(SERVER_ERROR_FRAME_ID);
  return false;
}

// creates the endpoint to fetch the data of the given frame
void constructFrameDataURL(char* url, const char* frameID, int bufferSize) {
  snprintf(url, bufferSize, "%s%s", frameDataBaseURL, frameID);
}

// Fetches frame data for a given frame ID and stores it in SPIFFS
void fetchAndStoreFrameData(HTTPClient& http, WiFiClientSecure& client, const char* frameID) {
  const int maxRetries = 3;
  const char* tempFilePath = "/temp_frame.bin";  // Single temporary file for all frames
  char finalFilePath[32];
  constructFilePath(finalFilePath, frameID, sizeof(finalFilePath));

  if (SPIFFS.exists(finalFilePath)) {
    Serial.print(F("File already exists: "));
    Serial.println(finalFilePath);
    return;
  }

  for (int attempt = 0; attempt < maxRetries; attempt++) {
    char url[100];
    constructFrameDataURL(url, frameID, sizeof(url));
    http.begin(client, url);
    int httpCode = http.GET();

    if (httpCode == HTTP_CODE_OK) {
      WiFiClient* stream = http.getStreamPtr();
      int bytesRead = stream->readBytes(frameDataBuffer, SIZE);
      if (bytesRead == SIZE) {
        // Fetch completed successfully, increment steps and display progress
        progressStepsCompleted++;
        updateAndDisplayProgress(progressStepsCompleted, totalProgressSteps, CRGB::Blue);

        // use a temp file in case there is an error mid-write
        // this protects against bad frame files with correct names
        saveFrameToTempFile(tempFilePath);

        SPIFFS.rename(tempFilePath, finalFilePath);
        Serial.print(F("Frame saved and renamed to: "));
        Serial.println(finalFilePath);

        progressStepsCompleted++;
        updateAndDisplayProgress(progressStepsCompleted, totalProgressSteps, CRGB::Blue);
        http.end();
        return;
      } else {
        Serial.print(F("Unexpected frame size: "));
        Serial.println(bytesRead);
      }
    } else {
      Serial.print(F("Frame data fetch attempt "));
      Serial.print(attempt + 1);
      Serial.print(F(" failed, HTTP code: "));
      Serial.println(httpCode);
    }
    http.end();
    delay(1000);
  }
  Serial.println(F("Failed to fetch frame data after retries."));
  displayErrorSymbol(SERVER_ERROR_FRAME_ID);
}

void saveFrameToTempFile(const char* filePath) {
  File file = SPIFFS.open(filePath, FILE_WRITE, true);
  if (!file) {
    Serial.print(F("Failed to open file for writing. Filename: "));
    Serial.println(filePath);
    return;
  }

  file.write(frameDataBuffer, SIZE);
  file.close();
}

void initializeFrameProgressVars() {
  size_t totalFrames = 0;
  for (JsonObject animation : jsonMetadata["metadata"].as<JsonArray>()) {
    totalFrames += animation["frameOrder"].as<JsonArray>().size();
  }
  totalProgressSteps = totalFrames * 2;
  progressStepsCompleted = 0;
}

// uses the given firmware version to check if there is a firmware update available
void checkOrUpdateFirmware(HTTPClient& http, WiFiClientSecure& client) {
  char versionCheckURL[256];
  snprintf(versionCheckURL, sizeof(versionCheckURL), "%s%s", firmwareURL, FIRMWARE_VERSION);

  Serial.print(F("Checking via firmware URL: "));
  Serial.println(versionCheckURL);

  http.begin(client, versionCheckURL);
  int httpCode = http.GET();

  if (httpCode == 200) {
    Serial.println(F("Firmware update available!"));

    Update.onProgress([](size_t done, size_t total) {
      updateAndDisplayProgress(done, total, CRGB::Green);
    });

    size_t contentLength = http.getSize();
    bool canBegin = Update.begin(contentLength);

    if (canBegin) {
      Serial.println(F("Starting firmware update"));
      WiFiClient* client = http.getStreamPtr();
      size_t written = Update.writeStream(*client);

      if (written == contentLength) {
        Serial.println(F("Update successfully completed. Rebooting..."));
        Update.end();
        ESP.restart();
      } else {
        Serial.println(F("Update failed."));
        Update.abort();
      }
    } else {
      Serial.println(F("Not enough space to begin firmware update"));
    }
  } else if (httpCode == 204) {
    Serial.println(F("Firmware is up to date"));
  } else if (httpCode == 501) {
    Serial.println(F("No firmware available on server."));
  } else if (httpCode == 502) {
    Serial.println(F("Error reading firmware file on server."));
  } else {
    Serial.print(F("Failed to check firmware version, HTTP code: "));
    Serial.println(httpCode);
  }
  http.end();
}

void saveMetadataToFile() {
  File file = SPIFFS.open(METADATA_FILE_NAME, FILE_WRITE);
  if (!file) {
    Serial.println(F("Failed to open metadata file for writing"));
    return;
  }

  WriteBufferingStream bufferedFile(file, 64);

  size_t metadataDocSize = measureJson(metadataDoc);
  size_t writtenLength = serializeJson(metadataDoc, bufferedFile);

  bufferedFile.flush();
  file.close();

  if (metadataDocSize == writtenLength) {
    Serial.println(F("Metadata saved to file. Saw expected length written."));
  } else {
    Serial.println(F("Did not see expected length written to metadata file."));
  }
}

// fetch new metadata, cleanup files, then fetch new frame data
bool fetchMetadataAndFrames(HTTPClient& http, WiFiClientSecure& client) {
  if (!fetchAndInitMetadata(http, client)) {
    Serial.println(F("Failed to fetch metadata."));
    return false;
  }

  cleanupUnusedFiles();

  initializeFrameProgressVars();

  // fetch frame data for each frame in the metadata
  for (JsonObject animation : jsonMetadata["metadata"].as<JsonArray>()) {
    const char* animationID = animation["animationID"].as<const char*>();
    Serial.print(F("Saving to SPIFFS: "));
    Serial.println(animationID);
    for (const char* frameID : animation["frameOrder"].as<JsonArray>()) {
      fetchAndStoreFrameData(http, client, frameID);
    }
  }

  saveMetadataToFile();
  return true;
}

bool loadMetadataFromFile() {
  File file = SPIFFS.open(METADATA_FILE_NAME, FILE_READ);
  if (!file) {
    Serial.println(F("Failed to open metadata file"));
    return false;
  }

  ReadBufferingStream bufferingStream(file, 64);
  DeserializationError error = deserializeJson(metadataDoc, bufferingStream);
  file.close();

  if (error) {
    Serial.print(F("deserialization of metadata from file failed: "));
    Serial.println(error.c_str());
    return false;
  } else {
    Serial.println(F("Successfully loaded metadata from save file."));
  }

  return true;
}

// check if the local metadata hash matches the server's hash
bool doesLocalMetadataMatchServer(HTTPClient& http, WiFiClientSecure& client) {
  // Check if the 'hash' key exists in the JSON object
  if (!jsonMetadata["hash"]) {
    Serial.println(F("Hash key not found in local metadata."));
    return false;
  }

  char hashCheckURL[256];
  snprintf(hashCheckURL, sizeof(hashCheckURL), "%s%s", metadataHashURL, jsonMetadata["hash"].as<const char*>());

  http.begin(client, hashCheckURL);
  int httpCode = http.GET();

  if (httpCode == HTTP_CODE_OK) {
    JsonDocument hashResponseDoc;
    deserializeJson(hashResponseDoc, http.getStream());
    bool hashesMatch = hashResponseDoc["hashesMatch"].as<bool>();
    http.end();
    return hashesMatch;
  } else {
    Serial.print(F("Failed to get hash response, HTTP code: "));
    Serial.println(httpCode);
    displayErrorSymbol(SERVER_ERROR_FRAME_ID);
  }

  http.end();
  return false;  // false if request fails or hashes don't match
}


void constructFilePath(char* filePath, const char* frameID, int bufferSize) {
  snprintf(filePath, bufferSize, "/%s.bin", frameID);
}

void updateAndDisplayProgress(size_t processed, size_t total, CRGB color) {
  if (total <= 0) return;  // Prevent division by zero

  // Calculate progress as a percentage and number of LEDs to light
  uint8_t progressPercent = (processed * 100) / total;
  uint16_t numLedsToLight = (processed * NUM_LEDS) / total;

  Serial.printf("%u%% ", progressPercent);

  // Update LEDs based on calculated progress
  for (int i = 0; i < NUM_LEDS; i++) {
    leds[i] = i < numLedsToLight ? color : CRGB::Black;
  }

  FastLED.show();
}

// delete any frame files that are not refferenced in the current metadata
// this will prevent storage from filling up
void cleanupUnusedFiles() {
  File root = SPIFFS.open("/");
  File file = root.openNextFile();

  size_t totalFiles = 0;
  while (file) {
    totalFiles++;
    file.close();
    file = root.openNextFile();
  }

  // Re-initialize to process the files after just counting the files
  root = SPIFFS.open("/");
  file = root.openNextFile();

  size_t processedFiles = 0;

  while (file) {
    const char* fileName = file.name();
    Serial.print(F("Assessing file for deletion: "));
    Serial.println(fileName);

    // skip metadata file and error frame files.
    if (strcmp(fileName, METADATA_FILE_NAME + 1) == 0 ||  // +1 needed to skip the slash
        strcmp(fileName, WIFI_ERROR_FILE_NAME) == 0 || strcmp(fileName, SERVER_ERROR_FILE_NAME) == 0 || strcmp(fileName, EMPTY_QUEUE_FILE_NAME) == 0) {
      Serial.println(F("Skipping essential file."));
      file.close();
      file = root.openNextFile();
      continue;
    }

    bool isUsed = false;
    for (JsonObject animation : jsonMetadata["metadata"].as<JsonArray>()) {
      for (const char* frameID : animation["frameOrder"].as<JsonArray>()) {
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
        Serial.print(F("Successfully deleted unused frame file: "));
      } else {
        Serial.print(F("Failed to delete file: "));
      }
      Serial.println(fullPath);
    }

    processedFiles++;
    updateAndDisplayProgress(processedFiles, totalFiles, CRGB::Red);

    file.close();
    file = root.openNextFile();
  }
  root.close();
}

// write the frame data buffer to a file at the given path
void saveFrameToSPIFFS(const char* filePath) {
  File file = SPIFFS.open(filePath, FILE_WRITE, true);
  if (!file) {
    Serial.print(F("Failed to open file for writing. Filename: "));
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
    Serial.print(F("Failed to open file for reading. Filename: "));
    Serial.println(filePath);
    return;
  }

  size_t bytesRead = file.read(frameDataBuffer, SIZE);
  if (bytesRead != SIZE) {
    Serial.print(F("Failed to read full frame from "));
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

      // Bounds checking to prevent memory corruption
      if (bufferIndex >= 0 && bufferIndex + 2 < SIZE) {
        leds[ledIndex] = CRGB(frameDataBuffer[bufferIndex], frameDataBuffer[bufferIndex + 1], frameDataBuffer[bufferIndex + 2]);
      } else {
        leds[ledIndex] = CRGB::Black;  // Safe fallback color
      }
    }
  }
  FastLED.show();
}

// DEBUG METHODS

void printAnimationMetadata() {
  for (JsonObject animation : jsonMetadata["metadata"].as<JsonArray>()) {
    const char* animationID = animation["animationID"].as<const char*>();
    int frameDuration = animation["frameDuration"];
    int repeatCount = animation["repeatCount"];

    Serial.print(F("Animation ID: "));
    Serial.println(animationID);
    Serial.print(F("Frame Duration: "));
    Serial.println(frameDuration);
    Serial.print(F("Repeat Count: "));
    Serial.println(repeatCount);

    Serial.print(F("Frame Order: "));
    for (const char* frameID : animation["frameOrder"].as<JsonArray>()) {
      Serial.print(frameID);
      Serial.print(F(" "));
    }
    Serial.println(F("\n-----"));
  }
}

void printFrameData() {
  Serial.println(F("Frame Data:"));
  for (size_t i = 0; i < SIZE; i++) {
    if (i > 0 && i % BYTES_PER_PIXEL == 0) {
      Serial.println();  // New line every 3 bytes (1 RGB LED)
    }
    Serial.print(F("0x"));
    if (frameDataBuffer[i] < 0x10) {  // Print leading zero for single digit hex numbers
      Serial.print(F("0"));
    }
    Serial.print(frameDataBuffer[i], HEX);  // Print byte in HEX
    Serial.print(F(" "));                   // Space between bytes
  }
  Serial.println(F("\n-------------------"));
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
    Serial.println(F("There was an error opening the file for writing"));
    return;
  }

  if (file.print(F("This is a test file."))) {
    Serial.println(F("File was written successfully"));
  } else {
    Serial.println(F("File write failed"));
  }

  file.close();
}

void connectToWiFi(const String& ssid, const String& password) {
  Serial.print(F("Connecting to "));
  Serial.print(ssid);
  Serial.println(F(" ..."));

  WiFi.begin(ssid.c_str(), password.c_str());

  Serial.print(F("Connecting to WiFi..."));
  size_t attemptCount = 0;
  size_t maxAttempts = 32;
  while (WiFi.status() != WL_CONNECTED && attemptCount < maxAttempts) {
    updateAndDisplayProgress(attemptCount, maxAttempts, CRGB::Orange);
    delay(1000);
    Serial.print(".");
    attemptCount++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println();
    Serial.println(F("WiFi connected successfully."));

    const boolean readOnly = false;
    preferences.begin("my-app", readOnly);

    // Put the SSID and password into storage
    preferences.putString("ssid", ssid);
    preferences.putString("password", password);

    preferences.end();  // Close the Preferences
  } else {
    Serial.println(F("Failed to connect to WiFi. Please check your credentials"));
    displayErrorSymbol(WIFI_ERROR_FRAME_ID);
  }
}

void reconnectWiFi() {
  const boolean readOnly = true;
  preferences.begin("my-app", readOnly);

  // If preferences contain ssid and password
  if (preferences.isKey("ssid") && preferences.isKey("password")) {
    String ssid = preferences.getString("ssid", "");
    String password = preferences.getString("password", "");

    WiFi.begin(ssid.c_str(), password.c_str());

    Serial.print(F("Reconnecting to WiFi..."));
    size_t attemptCount = 0;
    size_t maxAttempts = 32;
    while (WiFi.status() != WL_CONNECTED && attemptCount < maxAttempts) {
      updateAndDisplayProgress(attemptCount, maxAttempts, CRGB::Orange);
      delay(1000);
      Serial.print(".");
      attemptCount++;
    }

    if (WiFi.status() == WL_CONNECTED) {
      Serial.println(F("Reconnected."));
    } else {
      Serial.println(F("Reconnect failed."));
      displayErrorSymbol(WIFI_ERROR_FRAME_ID);
    }
  } else {
    Serial.println(F("No stored WiFi credentials."));
  }

  preferences.end();  // Close the Preferences
}

void setUpDNSServer(DNSServer& dnsServer, const IPAddress& localIP) {
  dnsServer.setTTL(3600);
  dnsServer.start(53, "*", localIP);
}

void startSoftAccessPoint(const char* ssid, const char* password, const IPAddress& localIP, const IPAddress& gatewayIP) {
  WiFi.mode(WIFI_MODE_AP);
  WiFi.softAPConfig(localIP, gatewayIP, subnetMask);
  WiFi.softAP(ssid, password);
  Serial.println(F("Access Point Started"));
  Serial.print(F("AP IP address: "));
  Serial.println(WiFi.softAPIP());
}

bool verifyServerConnectivity(const String& serverURLStr) {
  if (serverURLStr.isEmpty()) {
    Serial.println(F("Server URL is not set."));
    return false;
  }

  char pingURL[256];
  snprintf(pingURL, sizeof(pingURL), "%sping", serverURLStr.c_str());

  HTTPClient http;
  WiFiClientSecure client;
  http.useHTTP10(true);
  client.setInsecure();
  http.begin(client, pingURL);

  int httpCode = http.GET();

  if (httpCode == HTTP_CODE_OK) {
    String payload = http.getString();
    payload.trim();
    payload.replace("\"", "");
    if (payload == "pong") {
      Serial.println(F("Server ping successful."));
      http.end();
      return true;
    } else {
      Serial.print(F("Unexpected ping response: "));
      Serial.println(payload);
    }
  } else {
    Serial.print(F("Failed to ping server, HTTP code: "));
    Serial.println(httpCode);
  }

  http.end();
  return false;
}

const char setup_page[] PROGMEM = R"=====(
  <!DOCTYPE html>
  <html>
    <head>
      <title>ESP32 Setup</title>
      <style>
        #loading { display: none; }
      </style>
      <script>
        function showLoading() {
          document.getElementById("loading").style.display = "block";
          document.getElementById("form").style.display = "none";
        }
      </script>
    </head>
    <body>
      <h1>ESP32 Setup</h1>
      <div id="form">
        <form action="/setup" method="POST" onsubmit="showLoading()">
          SSID:<br>
          <input type="text" name="ssid"><br>
          Password:<br>
          <input type="password" name="password"><br>
          Server URL:<br>
          <input type="text" name="serverURL"><br><br>
          <input type="submit" value="Connect">
        </form>
      </div>
      <div id="loading">
        <h2>Trying to connect...</h2>
        <h3>See LED matrix for progress</h3>
      </div>
    </body>
  </html>
)=====";

const char success_page_template[] PROGMEM = R"=====(
  <h1>Success!</h1>
  <p>Your matrix is now connected to %s.</p>
  <p>Please close this page</p>
)=====";

const char failure_page_template[] PROGMEM = R"=====(
  <h1>Connection Failed</h1>
  <p>Could not connect using given %s. Please check your input and try again.</p>
  <p><a href="/">Try Again</a></p>
)=====";

bool setupComplete = false;

void setupWebServer(WebServer& server, const IPAddress& localIP) {
  size_t attemptCount = 0;

  server.on("/", HTTP_ANY, [&server]() {
    server.send_P(200, "text/html", setup_page);
  });

  server.on("/setup", HTTP_POST, [&server, &attemptCount]() {
    String ssid = server.arg("ssid");
    String password = server.arg("password");
    String serverURLStr = server.arg("serverURL");

    // Attempt to connect to WiFi
    connectToWiFi(ssid, password);

    // Check the WiFi connection status
    if (WiFi.status() != WL_CONNECTED) {
      char failure_page_content[512];
      snprintf(failure_page_content, sizeof(failure_page_content), failure_page_template, "WiFi credentials", "/");
      attemptCount = 0;
      server.send(200, "text/html", failure_page_content);
      return;
    }

    if (!serverURLStr.endsWith("/")) {
      serverURLStr += "/";
    }

    if (!verifyServerConnectivity(serverURLStr)) {
      char failure_page_content[512];
      snprintf(failure_page_content, sizeof(failure_page_content), failure_page_template, "server settings", "/");
      attemptCount = 0;
      server.send(200, "text/html", failure_page_content);
      return;
    }

    preferences.begin("my-app", false);
    preferences.putString("serverURL", serverURLStr);
    preferences.end();

    // If both WiFi and server verification are successful
    Serial.println(F("WiFi and server connection successful. Disconnecting from AP mode and sending success page."));

    char success_page[512];
    snprintf(success_page, sizeof(success_page), success_page_template, ssid.c_str());
    server.send(200, "text/html", success_page);

    delay(5000);
    WiFi.softAPdisconnect(true);
    setupComplete = true;
  });

  server.on("/connecttest.txt", [&server]() {
    server.sendHeader("Location", "http://logout.net", true);
    server.send(302);
  });

  server.on("/wpad.dat", [&server]() {
    server.send(404);
  });

  server.on("/generate_204", [&server]() {
    server.sendHeader("Location", localIPURL, true);
    server.send(302);
  });

  server.on("/redirect", [&server]() {
    server.sendHeader("Location", localIPURL, true);
    server.send(302);
  });

  server.on("/hotspot-detect.html", [&server]() {
    server.sendHeader("Location", localIPURL, true);
    server.send(302);
  });

  server.on("/canonical.html", [&server]() {
    server.sendHeader("Location", localIPURL, true);
    server.send(302);
  });

  server.on("/success.txt", [&server]() {
    server.send(200);
  });

  server.on("/ncsi.txt", [&server]() {
    server.sendHeader("Location", localIPURL, true);
    server.send(302);
  });

  server.on("/favicon.ico", [&server]() {
    server.send(404);
  });

  server.onNotFound([&server]() {
    server.sendHeader("Location", localIPURL, true);
    server.send(302);
  });


  server.begin();

  size_t maxAttempts = 256;
  while (!setupComplete && attemptCount < maxAttempts) {
    dnsServer.processNextRequest();
    server.handleClient();
    attemptCount++;
    updateAndDisplayProgress(attemptCount, maxAttempts, CRGB::Purple);
    delay(400);
  }

  server.close();
}

void fetchErrorSymbolsIfNeeded(HTTPClient& http, WiFiClientSecure& client) {
  fetchAndStoreFrameData(http, client, WIFI_ERROR_FRAME_ID);
  fetchAndStoreFrameData(http, client, SERVER_ERROR_FRAME_ID);
  fetchAndStoreFrameData(http, client, EMPTY_QUEUE_FRAME_ID);
}

void displayErrorSymbol(const char* symbol) {
  readFrameFromSPIFFS(symbol);
  for (int i = 0; i < 5; i++) {
    parseAndDisplayFrame();
    delay(750);
    FastLED.clear();
    FastLED.show();
    delay(500);
  }
}

void setup() {

  Serial.setTxBufferSize(1024);
  Serial.begin(115200);

  while (!Serial) {
    //wait
  }

  FastLED.addLeds<CHIPSET, DATA_PIN, COLOR_ORDER>(leds, NUM_LEDS);  // Init of the Fastled library
  FastLED.setBrightness(BRIGHTNESS);
  FastLED.clear(true);

  if (!SPIFFS.begin(true)) {
    Serial.println(F("An Error has occurred while mounting SPIFFS"));
    return;
  }

  // Serial.println("Clearing spiffs");
  // SPIFFS.format();
  // writeTestFile();
  // listSPIFFSFiles();

  reconnectWiFi();
  if (WiFi.status() != WL_CONNECTED) {
    startSoftAccessPoint(apSSID, apPassword, localIP, gatewayIP);
    setUpDNSServer(dnsServer, localIP);
    setupWebServer(server, localIP);
  }

  setURLsFromPreferences();

  Serial.println(F("Loading metadata from saved files."));
  if (SPIFFS.exists(METADATA_FILE_NAME)) {
    if (!loadMetadataFromFile()) {
      Serial.println(F("Failed to load metadata from file"));
    }
  } else {
    Serial.println(F("No saved metadata available."));
  }

  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    http.useHTTP10(true);
    WiFiClientSecure client;
    client.setInsecure();

    checkOrUpdateFirmware(http, client);
    lastFirmwareCheckMillis = millis();

    fetchErrorSymbolsIfNeeded(http, client);

    if (!doesLocalMetadataMatchServer(http, client)) {  // if metadata is not up to date, fetch new data
      Serial.println(F("Local metadata is out of date with server. Pulling new data."));
      fetchMetadataAndFrames(http, client);
    } else {
      Serial.println(F("Metadata matches server. No need to fetch new data."));
    }
    http.end();
    lastHashCheckMillis = millis();
  } else {
    Serial.println(F("Not connected to WiFi. Continuing offline."));
    displayErrorSymbol(WIFI_ERROR_FRAME_ID);
  }

  printAnimationMetadata();

  if (jsonMetadata["metadata"].as<JsonArray>().size() < 1) {
    while (true) {
      displayErrorSymbol(EMPTY_QUEUE_FRAME_ID);
      Serial.println(F("Queue is empty. Not advancing to main loop."));
    }
  }
}

void loop() {
  if (currentAnimationIndex < jsonMetadata["metadata"].as<JsonArray>().size()) {
    if (!animationLoaded) {  // If this is the first frame of the animation, load the animation
      currentAnimation = jsonMetadata["metadata"].as<JsonArray>()[currentAnimationIndex];
      const char* animationID = currentAnimation["animationID"];

      // Skip animations with invalid parameters
      int repeatCount = currentAnimation["repeatCount"].as<int>();
      JsonArray frameOrder = currentAnimation["frameOrder"].as<JsonArray>();

      if (repeatCount <= 0 || frameOrder.size() == 0) {
        Serial.print(F("Skipping invalid animation: "));
        Serial.println(animationID);
        currentAnimationIndex++;
        return;  // Skip this loop iteration
      }

      Serial.print(F("Displaying Animation: "));
      Serial.println(animationID);

      animationLoaded = true;
      currentRepeatCount = 0;

      // Enforce minimum frame duration to prevent CPU overload
      int frameDuration = currentAnimation["frameDuration"].as<int>();
      if (frameDuration < 16) frameDuration = 16;  // Minimum 16ms (~60 FPS)

      previousMillis = millis() - frameDuration;  // Reset timing for immediate first frame
    }

    // Enforce minimum frame duration in timing check too
    int frameDuration = currentAnimation["frameDuration"].as<int>();
    if (frameDuration < 16) frameDuration = 16;

    if (millis() - previousMillis >= frameDuration) {
      // Time to show the next frame
      JsonArray frameOrder = currentAnimation["frameOrder"].as<JsonArray>();
      if (currentFrameIndex < frameOrder.size()) {
        // If there are more frames to display
        const char* currentFrameID = frameOrder[currentFrameIndex];

        readFrameFromSPIFFS(currentFrameID);
        parseAndDisplayFrame();

        previousMillis = millis();  // save the last time a frame was displayed
        currentFrameIndex++;
      } else {
        // All frames displayed, but need to wait for frame duration before transitioning
        // Check if enough time has passed since the last frame was displayed
        if (millis() - previousMillis >= frameDuration) {
          // Check if the animation needs to be repeated
          if (currentRepeatCount < currentAnimation["repeatCount"].as<int>() - 1) {
            // Repeat the animation again
            currentRepeatCount++;
            currentFrameIndex = 0;                      // Reset frame index to restart the animation
            previousMillis = millis() - frameDuration;  // Reset timing for immediate repeat
          } else {
            // No more repeats, move to the next animation
            currentFrameIndex = 0;
            currentAnimationIndex++;
            animationLoaded = false;
            currentRepeatCount = 0;  // Reset repeat count for the next animation
          }
        }
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
      http.useHTTP10(true);
      WiFiClientSecure client;
      client.setInsecure();
      checkOrUpdateFirmware(http, client);
      http.end();
      lastFirmwareCheckMillis = millis();
    }
  } else if (millis() - lastHashCheckMillis >= hashCheckInterval) {
    checkOrConnectWifi();

    // Proceed with metadata check if connected to WiFi
    if (wifiState == CONNECTED) {
      HTTPClient http;
      http.useHTTP10(true);
      WiFiClientSecure client;
      client.setInsecure();
      if (!doesLocalMetadataMatchServer(http, client)) {
        Serial.println(F("Local metadata is out of date. Updating..."));
        fetchMetadataAndFrames(http, client);

        // Reset animation parameters if new metadata is fetched
        currentAnimationIndex = 0;
        currentFrameIndex = 0;
        animationLoaded = false;
      } else {
        Serial.println(F("Local metadata is up to date."));
      }
      http.end();
      lastHashCheckMillis = millis();
    }
  } else {
    delay(50);  // small delay to avoid hammering cpu. skip if we fetch new data
  }
}
