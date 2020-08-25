/*
Quiz node code based on ESP 8266. The node code can only talk with the ESP8266 based Quiz hub 

Will run WiFi client thats with the quiz hub server at http://quiz.local:

Communication between nodes and the hub uses json APIs. The API URL is http://quiz.local/quiz?par1=val2&par2=val2 and returns a
json document. Layout:
- when the hub calls a node: the node returns {"batt": 3000, "errCnt": 0, "btn": 0, "led": 0}. Meaning: battery voltage in mV, 
  the number of communication errors when calling the hub server and the current status of the button and LED
- when a node calls the hub: the hub returns {"nodeID": 1, "mode": 0, "won": 0, "err":0}. Meaning: node ID of this node as seen
  by the hub server, current play mode (Stop, Hot, Won, Test), if the node has won (1) or not (0) and if the mode can not 
  communicate with the hub (1=too many nodes)

---> Board used: nodemcu esp8266
---> Pinout https://components101.com/development-boards/nodemcu-esp8266-pinout-features-and-datasheet

*/

// Core Arduino ESP8266 libraries used
#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPClient.h>

// Additional custom libraries used (installed through Library Manager or from Github)
#include <ArduinoJson.h>    // use arduinojson library to generate API structures


// ---------------------- configuration parameters -------------------------
#define SERVERIP "192.168.4.1"   // the IP address of our quiz hub server
#define APSSID "quizhub"    // SSID of our advertised access point by hub
#define APPSK  ""           // Password of access point (empty is none)

// Multiplication factor to get from raw analogRead value to batt voltage
// This highly depends on your specific ESP8266 board. My boards have 2 onboard resistors between the
// internal A0 pin of the chip and board A0 connector, allowing it to read VCC directly on A0 (normally
// the A0 pin should be between 0..1V. However, this makes it not possible to monitor VCC using the
// internal setting (ADC_MODE(ADC_VCC);), so I am using a standard analogRead(A0) and multiply the 
// results by BATTMULT. The resistors on my board are configured to translate a VCC of 3V to an
// analogRead() value of 1024. I wanted to have a larger range to allow me to read VCC values up to 3.6V.
// So instead of directly connecting VCC to the A0 pin (which for my board would be fine), I added a 100k
// resistor between the VCC and board A0 pin.
// For VCC=3.3V, this results (on my board) in a voltage of 2.479V on the A0 board pin and 0.805V on the 
// A0 chip pin, which then results in a reading of 824 from analogRead.
// The BATTMULT value then calculates to 4.007 to report back 3301 as batt voltage.
// Other boards may not have resistors, which allow you to use ADC_MODE(ADC_VCC) instead.
// You can also connect your own set of resistors: 33k between A0 and GND and 100k between A0 and VCC
// and BATTMULT set to 3.943
#define BATTMULT 4.007      
// ADC_MODE(ADC_VCC);   // uncomment if your board does not have resistors connected to A0

// Hardware PIN connections
// during debugging/development you can define PIN_LED==PIN_SYSLED and just use the blue LED
#define PIN_SYSLED 2     // blue led 0=on, 1=off
#define PIN_LED 4        // main LED 0=off, 1=on, GPIO4/D2
//#define PIN_LED PIN_SYSLED // debug mode when hardware not yet connected
#define PIN_PRESS 0      // press button GPIO0/D3 and also the quiz button

// while in mode_hot status and candidates can press the button we flash the LED
#define FLASHON 100      // 100 ms on
#define FLASHOFF 1000    // 1000-100 = 900 ms off



ESP8266WebServer server(80);    // web server for normal Quiz function
const char* ssid = APSSID;
const char* password = APPSK;

IPAddress myIP;


// quiz node data
unsigned int errorsNode;    // number of errors as reported by node

typedef enum { mode_stop, mode_hot, mode_won, mode_lost, Mode_test } mode_q;          // The various modes
mode_q quizMode = mode_stop;

// command codes to nodes
#define CMD_STOP 0
#define CMD_HOT 1
#define CMD_WON 2
#define CMD_TEST 3


void setup(void) {
  pinMode(PIN_SYSLED, OUTPUT);
  pinMode(PIN_LED, OUTPUT);
  setLed(false);
  digitalWrite(PIN_SYSLED, 0); // turn on LED
  pinMode(PIN_PRESS,INPUT_PULLUP);
  pinMode(A0, INPUT);

  Serial.begin(115200);

  Serial.println();
  Serial.println("Quiz node");

  analogRead(A0); // dummy
  // WiFi.hostname(hostname);
  WiFi.mode(WIFI_STA);

  WiFi.begin(ssid, password);      
  Serial.print("Waiting for Wifi ");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }    
  Serial.println(" connected.");  
  Serial.print("Local IP address: ");
  myIP = WiFi.localIP(); 
  Serial.println(myIP);
  
  Serial.printf("Batt votage = %d\n", battVoltage());
 
  digitalWrite(PIN_SYSLED, 1); // turn off LED

  // configure the API url we will react to on our internal web server
  server.on("/", handleRoot);
  server.on("/quiz", handleHubAPI);
  server.onNotFound(handleNotFound);
  server.begin();
  Serial.println("Webserver for Quiz node started");

  send2hub(); // tell the hub we are ready
}

void loop(void) {
  static unsigned long flashtime = millis();    // for hardware switch stabilization

  // normal processing
  server.handleClient();

  switch (quizMode) {
    case mode_stop:
        setLed(false); // LED off
        break;
        
    case mode_hot:
        // flash LED
        if (millis()-flashtime < FLASHON) 
          setLed(true); // LED on
        else if (millis()-flashtime < FLASHOFF)
          setLed(false); // LED off
        else flashtime = millis();
        
        if (!digitalRead(PIN_PRESS) && flashtime+500 < millis()) {
          flashtime = millis();
          send2hub();
        }  
        break;

    case mode_won:
        setLed(true); // LED on
        break;
    
    case mode_lost:
        setLed(false); // LED off
        break;
    
  }
  
}



String callurl(String url) {
  WiFiClient client;
  HTTPClient http;
  String payload = "";

  if (http.begin(client, url)) {  // HTTP

    // Serial.print("[HTTP] GET..." + url + "\n");
    int httpCode = http.GET();

    // httpCode will be negative on error
    if (httpCode > 0) {
      // HTTP header has been send and Server response header has been handled
      // Serial.printf("[HTTP] GET... code: %d\n", httpCode);

      // file found at server
      if (httpCode == HTTP_CODE_OK || httpCode == HTTP_CODE_MOVED_PERMANENTLY) {
        payload = http.getString();
      }
    } else {
      Serial.printf("[HTTP] GET... failed, error: %s\n", http.errorToString(httpCode).c_str());
    }

    http.end();
  } else {
    Serial.printf("[HTTP] Unable to connect\n");
  }
  return payload;
}

void handleHubAPI() {
  // reacts to /quiz?cmd=xx and return json {"batt": 3000, "errCnt": 0, "btn": 0, "led": 0}
  // if mode==mode_hot we declare a winner
  uint8_t i;
  const size_t capacity = JSON_OBJECT_SIZE(4) + 61;
  StaticJsonDocument<capacity> doc;
  digitalWrite(PIN_SYSLED, LOW); // turn on
  doc["batt"] = battVoltage();
  doc["errCnt"] = errorsNode;
  doc["btn"] = digitalRead(PIN_PRESS);
  doc["led"] = digitalRead(PIN_LED);

  // Serial.printf("# args = %d\n", server.args());
  for (i = 0; i < server.args(); i++) {
    // Serial.printf("=> %s: %s\n", server.argName(i).c_str(), server.arg(i).c_str());
    if (server.argName(i) == "cmd") {
      // Serial.printf("Command = %s\n", server.arg(i).c_str());
      quizMode = (mode_q) server.arg(i).toInt();
    }
  }
  char output[128];
  serializeJson(doc, output);

  // Serial.printf("Sending: %s\n ", output);
  server.send(200, "application/json", output);
  digitalWrite(PIN_SYSLED, HIGH); // turn off
}


void send2hub() {
  // send http://[IP_address]/quiz to hub (directly to IP address)
  // should return {"nodeID": 1, "mode": 0, "won": 0, "err":0}
  String htmltxt = callurl("http://" SERVERIP "/quiz");
  const size_t capacity = JSON_OBJECT_SIZE(4) + 61;
  StaticJsonDocument<capacity> doc;
  // Parse returned JSON object
  DeserializationError error = deserializeJson(doc, htmltxt);
  if (error) {
    Serial.printf("Json deserializeJson() failed: %s ", error.c_str());
    errorsNode++;
    return;
  }
  if (doc.containsKey("nodeID")) {
    // we probably have a right json object returned
    // Serial.printf("Json data returnd nodeID=%s mode=%d \n", doc["nodeID"].as<String>().c_str(), doc["mode"].as<int>());
    quizMode = (doc["won"].as<int>() > 0 ? mode_won : mode_lost); 
  } else {
    errorsNode++;
    Serial.println("Invalid json returned");
  }
}

void handleRoot() {
  digitalWrite(PIN_SYSLED, LOW); // on
  Serial.println("Root");
  Serial.println(server.client().remoteIP());
  server.send(200, "text/plain", "hello from Quiz node!");
  delay(100);
  digitalWrite(PIN_SYSLED, HIGH); // off
}

void handleNotFound() {
  digitalWrite(PIN_SYSLED, LOW); // on
  String message = "Invalid URL\n\n";
  message += "Quiz URI: ";
  message += server.uri();
  Serial.println(message);
  server.send(404, "text/plain", message);
  digitalWrite(PIN_SYSLED, 0); // off
}

void setLed(bool mode) {
  if (PIN_LED==PIN_SYSLED) {
    // during debugging/development you can define these LED as equal and just use the blue LED
    digitalWrite(PIN_SYSLED, !mode); // on/off are switched 
  } else {
    digitalWrite(PIN_LED, mode);      
  }
}

int battVoltage() {
  int v = analogRead(A0);
  // Serial.printf("analogRead %d\n", v);
  return (int) ((float) v * BATTMULT);
}
