# LEDmatrix

## Getting set up

1. Download arduino IDE

    Go to the [Arduino website](https://www.arduino.cc/en/software) and download the IDE

2. Enable use of ESP32 board in arduino IDE

    Follow [this tutorial](https://randomnerdtutorials.com/installing-the-esp32-board-in-arduino-ide-windows-instructions/). Your board should be "ESP32 Dev Module" when prompted.

3. Add ability for Spiffs upload to arduino IDE

    Follow [this tutorial](https://randomnerdtutorials.com/install-esp32-filesystem-uploader-arduino-ide/)
    
4. [Download software](https://sourceforge.net/projects/lcd-image-converter/) to convert images to array of color values

    This is a bit wonky to get set up and will be easier to describe over video call/in person. Will describe how to use this further down.

## Overview of how the matrix works

### Hardware

Knowledge of hardware isn't super important at this point but [this is a good overview](https://www.youtube.com/watch?v=_0a9JZLGu4M&list=LL&index=45&t=151s) of how I built our matrices.

### Files

Tried to make filenames somewhat meaningful but ill make a list to describe them here. Most are just files used for testing various components and are not really used anymore.

*animationSample* - sample of basic matrix function - displaying images based on hardcoded color value array

*imageTest* - precursor to *animationSample* does the same thing just not as clean. Uses a different method to change pixel values. Not really used anymore.

*imgFromFile* - test to read an image from a txt file

*imgFromJson* - test to read an image from a json file

*jsonManipulationtest* - sample to read and write to a json file

*jsonManipulationdemo* - test to implement read and write to a json file

*jsonTest* - most current animation method. Reads whole sequence of animations from json file and accounts for differing nums of images, timings, etc.

*LEDmatrixWebServer* - First attempt on using webpage to update animation data. Takes text from the user and then tries to save it to the file system on the ESP32 but doesn't really work yet.

### Using spiffs

After completing step 3, you should be able to upload to spiffs. The file you want to upload should be in a subdirectory named `data`. All the json using scripts serve as an example. To upload that file, just go to *Tools > ESP32 Sketch data upload* in the Arduino IDE.

### Using the manual image conversion tool

Once you install, open the softare and hit new image. At this point you should already have your 16px by 16px image unless you finna draw your own in the software. Sidenote - windows 'Photos' does a pretty good job of resizing by right clicking and resizing. 

Go to *Image > Import* then select your pic. Then go to *Options > Conversion*. In the prepare tab, Set `Type: Color` then select `Use custom script` and paste this in the box :
```
for (var y = 0; y < image.height; y++) {
    if(y%2==0) {
        for (var x = 0; x < image.width; x++) {
            image.addPoint(x, y);
        }
    } else {
        for (var x = image.width-1; x >=0 ; x--) {
            image.addPoint(x, y);
        }
    }
}
```
This just alternates the order of every other line since the LED strips are alternating in on the matrix. Next go to the Image tab. Put `"0x` as the prefix and `"` as the suffix. This is to adhere to the JSON list formatting. Then set `Block size: 24-bit`. This will give us the hex value that we need. Everything else should stay the same. Save this preset.

Now click the `Show preview` button in the bottom left and then copy and paste that into the JSON file.
