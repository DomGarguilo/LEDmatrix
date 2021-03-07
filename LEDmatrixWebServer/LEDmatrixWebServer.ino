#include <FastLED.h>
#include <ArduinoJson.h>
#include <SPIFFS.h>
#include <Arduino.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>

//variables for LED setup
#define NUM_LEDS 256
#define COLOR_ORDER GRB
#define CHIPSET WS2812B
#define BRIGHTNESS 2
#define DATA_PIN 13  // Connected to the data pin of the first LED strip

AsyncWebServer server(80);

// REPLACE WITH YOUR NETWORK CREDENTIALS
const char* ssid = "Asparagus";
const char* password = "CoolKid123!";

const char* NAME = "name";
const char* FRAME_DURATION = "frameDuration";
const char* REPEAT_COUNT = "repeatCount";
const char* FRAMES = "frames";

// Define the array of leds
CRGB leds[NUM_LEDS];
const char *fileName = "/data.json";
File myFile;
//JsonObject doc;

// HTML web page to handle 3 input fields (input1, input2, input3)
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML><html><head>
  <title>ESP Input Form</title>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  </head><body>
  <form action="/get">
    Name: <input type="text" name="name"><br>
    Frame Duration: <input type="text" name="frameDuration"><br>
    Repeat Count: <input type="text" name="repeatCount"><br>
    Frame Data: <input type="text" name="frames"><br>
    <input type="submit" value="Submit">
  </form><br>
  </body></html>)rawliteral";

void notFound(AsyncWebServerRequest *request) {
  request->send(404, "text/plain", "Not found");
}

void initWifi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  if (WiFi.waitForConnectResult() != WL_CONNECTED) {
    Serial.println("WiFi Failed!");
    return;
  }
  Serial.println();
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());
}

void initRequests() {
  // Send web page with input fields to client
  server.on("/", HTTP_GET, [](AsyncWebServerRequest * request) {
    request->send_P(200, "text/html", index_html);
  });

  // Send a GET request to <ESP_IP>/get?input1=<inputMessage>
  server.on("/get", HTTP_GET, [] (AsyncWebServerRequest * request) {
    String nameData, nameLabel;
    String durationData, durationLabel;
    String repeatData, repeatLabel;
    String frameData, frameLabel;

    // GET input1 value on <ESP_IP>/get?input1=<inputMessage>
    if (request->hasParam(NAME) && request->hasParam(FRAME_DURATION) && request->hasParam(REPEAT_COUNT) && request->hasParam(FRAMES)) {
      // set data values
      nameData = request->getParam(NAME)->value();
      durationData = request->getParam(FRAME_DURATION)->value();
      repeatData = request->getParam(REPEAT_COUNT)->value();
      frameData = request->getParam(FRAMES)->value();
      // set label values
      nameLabel = NAME;
      durationLabel = FRAME_DURATION;
      repeatLabel = REPEAT_COUNT;
      frameLabel = FRAMES;
    } else {
      //set data values
      nameData = durationData = repeatData = frameData = "No message sent";
      // set label values
      nameLabel = durationLabel = repeatLabel = frameLabel = "none";
    }
    Serial.println(nameData);
    Serial.println(durationData);
    Serial.println(repeatData);
    Serial.println(frameData);
    request->send(200, "text/html", "HTTP GET request sent to your ESP on input field"
                  "<br>(" + nameLabel + ") with value: " + nameData +
                  "<br>(" + durationLabel + ") with value: " + durationData +
                  "<br>(" + repeatLabel + ") with value: " + repeatData +
                  "<br>(" + frameLabel + ") with value: " + frameData +
                  "<br><a href=\"/\">Return to Home Page</a>");
  });

  server.onNotFound(notFound);
  server.begin();
}

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

void displayImage() {
  //initialize json object
  DynamicJsonDocument json (50948);
  JsonObject doc;
  doc = getJSonFromFile(&json, fileName);

  //get and print number of animations found
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

void setup() {
  
  // LED matrix setup
  FastLED.addLeds<CHIPSET, DATA_PIN, COLOR_ORDER>(leds, NUM_LEDS).setCorrection(TypicalSMD5050); // Init of the Fastled library
  FastLED.setBrightness(BRIGHTNESS);
  FastLED.clear(true);

  // Initialize serial port
  Serial.begin(115200);
  while (!Serial) continue;

  // File system setup
  if (!SPIFFS.begin(true)) {
    Serial.println(F("An Error has occurred while mounting SPIFFS"));
    return;
  }

  // setup web server
  initWifi();

  // setup get requests from server
  initRequests();

  // displays images on LED matrix from JSON file
  displayImage();

}

void loop() {

}
