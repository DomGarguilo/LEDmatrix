/*
  Write JSON file to SD
  by Renzo Mischianti <https://www.mischianti.org>

  https://www.mischianti.org/
*/

#include <ArduinoJson.h>
#include <SPIFFS.h>

const int chipSelect = 4;

const char *filename = "/data.json";  // <- SD library uses 8.3 filenames

File myFileSDCart;

JsonObject getJSonFromFile(DynamicJsonDocument *doc, String filename, bool forceCleanONJsonError = true ) {
  // open the file for reading:
  myFileSDCart = SPIFFS.open(filename);
  if (myFileSDCart) {
    // read from the file until there's nothing else in it:
    //          if (myFileSDCart.available()) {
    //              firstWrite = false;
    //          }

    DeserializationError error = deserializeJson(*doc, myFileSDCart);
    if (error) {
      // if the file didn't open, print an error:
      Serial.print(F("Error parsing JSON "));
      Serial.println(error.c_str());

      if (forceCleanONJsonError) {
        return doc->to<JsonObject>();
      }
    }

    // close the file:
    myFileSDCart.close();

    return doc->as<JsonObject>();
  } else {
    // if the file didn't open, print an error:
    Serial.print(F("Error opening (or file not exists) "));
    Serial.println(filename);

    Serial.println(F("Empty json created"));
    return doc->to<JsonObject>();
  }

}

/**
   Function to serialize file to SD
   by Renzo Mischianti <https://www.mischianti.org>
   example:
   boolean isSaved = saveJSonToAFile(&doc, filename);
*/
bool saveJSonToAFile(DynamicJsonDocument *doc, String filename) {
  SPIFFS.remove(filename);

  // open the file. note that only one file can be open at a time,
  // so you have to close this one before opening another.
  Serial.println(F("Open file in write mode"));
  myFileSDCart = SPIFFS.open(filename, FILE_WRITE);
  if (myFileSDCart) {
    Serial.print(F("Filename --> "));
    Serial.println(filename);

    Serial.print(F("Start write..."));

    serializeJson(*doc, myFileSDCart);

    Serial.print(F("..."));
    // close the file:
    myFileSDCart.close();
    Serial.println(F("done."));

    return true;
  } else {
    // if the file didn't open, print an error:
    Serial.print(F("Error opening "));
    Serial.println(filename);

    return false;
  }
}

// Prints the content of a file to the Serial
void printFile(const char *filename) {
  // Open file for reading
  File file = SPIFFS.open(filename);
  if (!file) {
    Serial.println(F("Failed to read file"));
    return;
  }

  // Extract each characters by one by one
  while (file.available()) {
    Serial.print((char) file.read());
  }
  Serial.println();

  // Close the file
  file.close();
}

void setup() {
  // Initialize serial port
  Serial.begin(115200);
  while (!Serial) continue;

  delay(500);

  // Initialize SD library
  while (!SPIFFS.begin(true)) {
    Serial.println(F("Failed to initialize SD library"));
    delay(1000);
  }

  Serial.println(F("SD library initialized"));
  printFile(filename);/*
  DynamicJsonDocument doc(20000);

  JsonObject obj;
  obj = getJSonFromFile(&doc, filename);

  JsonArray data = obj[F("images")];

  // create an object to add to the array
  JsonObject objArrayData = data.createNestedObject();

  objArrayData["name"] = "Enwerd";
  objArrayData["frameDuration"] = 2000;
  objArrayData["repeatCount"] = 3;
  boolean isSaved = saveJSonToAFile(&doc, filename);

  if (isSaved) {
    Serial.println("File saved!");
  } else {
    Serial.println("Error on save File!");
  }

  // Print test file
  Serial.println(F("Print updated file..."));
  printFile(filename);

  delay(5000);*/

}

void loop() {
  // Allocate a temporary JsonDocument
  // Don't forget to change the capacity to match your requirements.
  // Use arduinojson.org/v6/assistant to compute the capacity.
  //  StaticJsonDocument<512> doc;
  // You can use DynamicJsonDocument as well

}
