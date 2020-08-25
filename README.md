# Quiz buttons based on Arduino AVR or ESP8266 architecture

This project was based on the need to create a set of "Quiz" buttons for a fun family evening to support a Quiz setting, where multiple teams have to answer quiz questions and you need to know who presses the Quiz-button first.

## Overview

I bought 4 large pushbuttons on Sparkfun (see https://www.sparkfun.com/products/9181) and mounted the buttons on a piece of plywood, supported by some large screws. The Arduino hardware for each button is mounted below. See

![Quiz Button overview](img/QuizButton_Overview.png?raw=true "Quiz Button Overview")

Over time I created 3 generations:
1. Fully wired - using UTP cable to connect each button to a central Raspberry PI based hub
2. Wireless using nRF24L01 radio modules and an AVR based Arduino (Arduino Pro Mini ATMEGA328) using 2.4 Mhz communication
3. Wireless using ESP8266 using standard WiFi communication

The Github repository contains the 2nd and 3rd generation:
- [QuizNode](QuizNode) - Arduino code for the buttons of the 2nd generation (Arduino Pro Mini ATMEGA328/ nRF24L01 based)
- QuizHub - Arduino code for the central hub of the 2nd generation (Arduino Pro Mini ATMEGA328/ nRF24L01 based with 4 segment display)
- QuizNode8266 - Arduinocode for the buttons of the 3rd generation (ESP8266 based)
- QuizHub8266 - Arduino code for the central hub of the 3rd generation (ESP8266 based, using a web interface)

Please note you can not mix 2nd and 3rd generation buttons and hub, as the communication protocol is different.

## Hardware 2nd generation (AVR)

The quiz button is powered by a single 1,5V battery. The hardware has a voltage booster to create 3.3V working power to the Atmega328 and nRF24L01. Additional power line cleaning is added using a coil and capacitor to ensure the nRF24L01 gets a stable power supply. The large button has a LED and resistor built in that expects a 12V power supply. The Arduino can provide the current directly to the LED, but the resistor needs to be modified to handle the 3.3V power supply.

![Quiz Button back](img/QuizButton_AVR.png?raw=true "Quiz Button back")

The quiz hub has a 4 digit 7 segment display to show status and results and uses hardware push buttons to start and stop the quiz questions.

![Quiz Hub](img/QuizHub_AVR.png?raw=true "Quiz Hub")

Hardware schematics will be added soon


## Hardware 3nd generation (ESP8266)

The quiz button is powered by 2 AA batteries making the supply 3V (on average). Since the ESP8266 can not drive directly 20mA to the button LED, I added a current limiting circuit using a single NPN transistor. This requires some manual tuning based on the hFE of the selected transistor.

![Quiz Button back](img/QuizButton_ESP8266.png?raw=true "Quiz Button back")

The hub is powered by a standard USB connector/ power supply. The gane can be activated using buttons or using the web interface of the built in web server.

![Quiz Hub](img/QuizHub_ESP8266.png?raw=true "Quiz Hub")

Hardware schematics will be added soon





