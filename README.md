# LEDmatrix

![demo video of the LED matrix](https://i.imgur.com/TyKAa4D.gif)

Control your Esp32 based LED matrix remotely. This firmware works in conjunction with a web app ([github.com/DomGarguilo/LEDserver](https://github.com/DomGarguilo/LEDserver)) to allow us to upload animations on the web and have them pulled and displayed on the LED matrix.

## Getting the firmware uploaded to your matrix

1. Go to the [Arduino website](https://www.arduino.cc/en/software) and download the IDE

2. Follow [this tutorial](https://randomnerdtutorials.com/installing-the-esp32-board-in-arduino-ide-windows-instructions/) to enable use of ESP32 board in arduino IDE.

3. Clone this repo and open `main/` in the Arduino IDE
   
4. Create a copy of `secrets-example.h` and rename it to `secrets.h`. Edit the file to add your wifi credentials and the URL to your [LEDserver](https://github.com/DomGarguilo/LEDserver).
   
5. Flash `main.ino` to your Esp32 from the arduino IDE. You may need to hold the boot button while the console reads `Connecting...`

## Status visual key
Loading animations indicate different processes. If the loading animation color is...
* Green: firmware is updating
* Blue: fetching and saving new data
* Red: old files are being cleaned up