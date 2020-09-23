# Quiz buttons based on Arduino AVR or ESP8266/ESP32 architectures

This project was based on the need to create a set of "Quiz" buttons for a fun family evening to support a Quiz setting, where multiple teams have to answer quiz questions and you need to know who presses the Quiz-button first.

## Overview

I bought 4 large pushbuttons on Sparkfun (see https://www.sparkfun.com/products/9181) and mounted the buttons on a piece of plywood, supported by some large screws. The Arduino hardware for each button is mounted underneath.

![Quiz Button overview](img/QuizButton_Overview.png?raw=true "Quiz Button Overview")

Over time I created 3 generations:
1. Fully wired - using UTP cable to connect each button to a central Raspberry PI based hub
2. Wireless using nRF24L01 radio modules and an AVR based Arduino (Arduino Pro Mini ATMEGA328) using 2.4 Mhz communication
3. Wireless using ESP8266 using standard WiFi communication (optional use ESP32 for the quiz hub)

This Github repository contains the code for the 2nd and 3rd generation:
- [QuizNode](QuizNode) - Arduino code for the buttons of the 2nd generation (Arduino Pro Mini ATMEGA328/ nRF24L01 based)
- [QuizHub](QuizHub) - Arduino code for the central hub of the 2nd generation (Arduino Pro Mini ATMEGA328/ nRF24L01 based with 4 segment display)
- [QuizNode8266](QuizNode8266) - Arduinocode for the buttons of the 3rd generation (ESP8266 based)
- [QuizHub8266](QuizHub8266) - Arduino code for the central hub of the 3rd generation (ESP8266 based, using a web interface)
- [QuizHubEsp32](QuizHubEsp32) - Arduino code for the central hub of the 3rd generation (ESP32 based, using a web interface). This version handles quiz requests more reliable/ honest in the heat of the game due to the faster CPU
and improved WiFi connection

Please note you can not mix 2nd and 3rd generation buttons and hub, as the communication protocol is different.

## Hardware 2nd generation (AVR)

The quiz button is powered by a single 1,5V battery. The hardware has a voltage booster to create 3.3V working power to the Atmega328 and nRF24L01. Additional power line cleaning is added using a coil and capacitor to ensure the nRF24L01 gets a stable power supply. The large button has a LED and resistor built in that expects a 12V power supply. The Arduino can provide the current directly to the LED, but the resistor needs to be modified to handle the 3.3V power supply.

![Quiz Button back](img/QuizButton_AVR.png?raw=true "Quiz Button back")

The quiz hub has a 4 digit 7 segment display to show status and results and uses hardware push buttons to start and stop the quiz questions.

![Quiz Hub](img/QuizHub_AVR.png?raw=true "Quiz Hub")

Hardware schematics will be added soon


## Hardware 3nd generation (ESP8266/ESP32)

The quiz buttons are ESP8266 based. For the Quiz hub you can choose for the ESP8266 or ESP32. The ESP32 is a better choice as I found this one to be more reliable.

The quiz button is powered by 2 AA batteries making the supply 3V (on average). Since the ESP8266 can not drive directly 20mA to the button LED, I added a current limiting circuit using a single NPN transistor. This requires some manual tuning based on the hFE of the selected transistor. The current limiting resistor that was added to the LED now needs to be removed.

![Quiz Button back](img/QuizButton_ESP8266.png?raw=true "Quiz Button back")

The ESP8266 or ESP32 hub is powered by a standard USB connector/ power supply. The gane can be activated using buttons or using the web interface of the built in web server.

![Quiz Hub](img/QuizHub_ESP8266.png?raw=true "Quiz Hub")

Hardware schematics will be added soon

## Getting started wuth the 3rd generation (ESP8266/ESP32)

Starting steps:
- Get enough ESP8266 boards (you need 1 for each button) and a board for the hub (ESP32 or ESP8266). I am using the NodeMCU-32S
- Download this repository
- Prepare your Arduino environment for ESP8266 See https://arduino-esp8266.readthedocs.io/en/latest/ and for the ESP32 environment (make sure you have version 1.0.4 or higher) and add this URL to your board manager: https://dl.espressif.com/dl/package_esp32_index.json

Steps for the hub:
- Add any additional MP3 files to the data/sounds folder you may want to play when a quiz button is played
- ESP8266 hub: Install the data upload tool to ensure you can upload the files in the [data folder](QuizHub8266/data) to the ESP8266 you will use as hub. See [here](https://arduino-esp8266.readthedocs.io/en/latest/filesystem.html) and [here](
https://github.com/earlephilhower/arduino-esp8266littlefs-plugin/releases)
- ESP32 hub:  Install the data upload tool to ensure you can upload the files in the [data folder](QuizHubEsp32/data) to the ESP32 you will use as hub. See [here](https://randomnerdtutorials.com/install-esp32-filesystem-uploader-arduino-ide/)
- Upload the data folder to your ESP8266 or ESP32 using the tool above 
- Compile and upload the QuizHub8266 or QuizHubEsp32 sketch
- Point your phone or laptop to the access point "quizhub" and point your browser to http://192.168.4.1
- Configure your local Wifi network and wait for the quizhub to reboot
- You can now get to the quizhub from your normal Wifi network by pointing ypur browser to http://quiz.local

Steps for each button:
- Add the circuit to allow measurement of the battery voltage (optional) and current driver for the external LED
- Compile and upload the QuizNode8266 sketch (optionally change the source in case you want to the use the built in LED instead of an external LED)
- Once the button restarts, you should see en entry appear on the web server interface of the hub and you can assign a name and MP3 file to play when the given button wins. The MP3 file will be played on the phone or web browser used to control the quiz hub



