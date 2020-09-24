/*
Quiz hub code based on ESP32. The hub code can only talk with Quiz hub ESP8266 nodes.
Developed for the NodeMCU-ESP-32S. to programm, press the Boot switch

The versions of the source code for the ESP8266 and ESP32 is almost identical. Differences:
- ESP32 does not support NAT out of the box (see below)
- blue led pin is revered on/off on my esp32 board
- connection pins for manual switches are different (GPIO assignment)

Will run Access Point with SSID of quizhub that runs a web server for:
- communication with the Quiz Nodes (buttons)
- handle config start/stop commands from a generic web browser (PC or mobile phone) to operate the quiz server.
  The web browser does not need to know the IP address and can go to the URL: http://quiz.local to get to the server

During boot operation we will also try to connect to a public WiFi access point. If this is succesfull a NAT router is setup
to this public WiFi access point, so all clients of our own Access Point can also get to the internet.
The PC/Mobile web browser has a choice to connect to our AP (and then still access the internet) or keep being connected
to the existing SSID of the public WiFi access point to access the internet and just use the IP address allocated 
by the public WiFi access point to address the quiz server.

Either way the URL http://quiz.local will work

If during boot the sketch can not connect to the WiFi access point, the captive portal from the WiFiManager is started
and the user can configure and store the WiFi parameters. If nothing is done within 40 seconds, the normal quiz server is
started (access point only mode) and all clients need to connect to SSID quizhub.
If the switch on WIFIPORTALPIN is pressed during boot the captive portal the Wifi settings are wiped and the captive portal
is brought up as well.

The URL of the captive portal is not http://quiz.local, because MDNS does not run then. The IP address is fixed to 192.168.4.1
You could define WM_MDNS in WiFiManager.h in your library folder, but I do not like changing lib h files

Communication between nodes and the hub uses json APIs. The API URL is http://quiz.local/quiz?par1=val2&par2=val2 and returns a
json document. Layout:
- when the hub calls a node: the node returns {"batt": 3000, "errCnt": 0, "btn": 0, "led": 0}. Meaning: battery voltage in mV, 
  the number of communication errors when calling the hub server and the current status of the button and LED
- when a node calls the hub: the hub returns {"nodeID": 1, "mode": 0, "won": 0, "err":0}. Meaning: node ID of this node as seen
  by the hub server, current play mode (Stop, Hot, Won, Test), if the node has won (1) or not (0) and if the mode can not 
  communicate with the hub (1=too many nodes)

We use SPIFFS to define a fie system to store files we need in the html interface. Things like html, css, js files and
images. The files are in the data folder of the Arduino sketch and you need to install the SPIFFS data upload tool to upload
the data folder to the internal flash memory of the ESP32. The upload tool, once installed, will show up in the Tools menu
of your Arduino environment as extra menu item "ESP32 Sketch data upload". See these links:
- https://github.com/me-no-dev/arduino-esp32fs-plugin

NAT support. 
Version 1.0.4 of ESP32 core does not support NAT (like ESP8266 does). This means that nodes connected to the soft AP side can not
reach the internet. This is fine for the quiz nodes and the web browser can connect to the quiz hub through the second IP address provided
by the DHCP server of the local Wifi hotspot you are connected to. However, when this is not possible and you want to connect to
the quizhub SSID and get the rest of the JS files from the Internet, you should configure a NAT based router. For ESP core
1.0.4, 3rd party NAT support is provided here: https://github.com/Lucy2003/ESP32-NAPT-For-Arduino. Then uncomment the NAT_SUPPORT
directive.

Still todo:
- If the server is rebooted the clients do not automatically reconnect (Wifi issue)
- http://quiz.local mdns is only advertised on the host network (not our own network)
- index.html:
  - create button for test mode

*/

// #define NAT_SUPPORT

// Core Arduino ESP32 libraries used
#include "FS.h"
#include "SPIFFS.h"
#include <WiFi.h>
#include <WiFiMulti.h>  // not sure we need this
#include <WiFiClient.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <HTTPClient.h>

#include <lwip/dns.h>

#ifdef NAT_SUPPORT
  #include "lwip/lwip_napt.h"
  #define STATION_IF      0x00
  #define SOFTAP_IF       0x01
#endif


// Additional custom libraries used (installed through Library Manager or from Github)
#include <WiFiManager.h>    // https://github.com/tzapu/WiFiManager captive portal to connect to WiFi
#include <ArduinoJson.h>    // use arduinojson library to generate API structures


// ---------------------- configuration parameters -------------------------
#define QUIZSERVER "quiz"   // the name on the local network of our quiz hub server will be http://quiz.local
#define APSSID "quizhub"    // SSID of our advertised access point for the quiz nodes
#define APPSK  ""           // Password of access point (empty is none)

// Hardware PIN connections
#define LEDBLUEPIN 2            // blue led (on board LED and also extern connected to GPIO02) 1=on, 0=off
#define LEDGREENPIN 22      // green LED: when a node has won
#define LEDREDPIN 23        // red LED: when "hot" (nodes are flashing)
#define LEDON 1             // for LEDs: value to digitalwrite to turn led off
#define LEDOFF 0            // for LEDs: value to digitalwrite to turn led on
#define PIN_HOT 5           // enter hot mode to all nodes GPIO5 => GPIO5 mark
#define PIN_STOP 18         // set stop mode to all nodes GPIO4 => GPIO18
#define PIN_STATUS 19       // show statistics of all nodes and run connection test (will turn on/off all button LEDs) GPIO19
#define SWITCH_OFF 0        // DigitalRead for PIN_HOT, PIN_STOP, PIN_STATUS to show the switch is not pressed
#define SWITCH_ON 1         // idem for switch is pressed
#define WIFIPORTALPIN PIN_HOT   // trigger WiFi portal during reboot 

// We have 2 webservers defined on port 80, of which only 1 runs at a time
WebServer server(80);    // web server for normal Quiz function
WiFiManager wm;                 // web server for captive portal to configure WiFi parameters
const char* ssid = APSSID;
const char* password = APPSK;
const char* hostname = QUIZSERVER;

IPAddress myIP;

// quiz nodes
#define MAXNODES 5
IPAddress IPNode[MAXNODES];           // IP addresses of all nodes
unsigned int nodeCnt = 0;             // number of nodes we know of
unsigned int packets;                 // number of packets sent to all nodes (excl ACKs)
bool nodeActive[MAXNODES];            // we were able to succesfully send to node, so it probably exists
unsigned int battValue[MAXNODES];     // battery voltage in mV
unsigned int wins[MAXNODES];          // number of wins
unsigned int errorsAcc[MAXNODES];     // number of times a send command did not get an ACK back
unsigned int errorsHot[MAXNODES];     // number of times a send CMD_HOT command returned an error
unsigned int errorsStop[MAXNODES];    // number of times a send CMD_STOP command returned an error
unsigned int errorsNode[MAXNODES];    // number of errors as reported by node
unsigned int lateWins[MAXNODES];      // number of times a winning node did not get its winning ACK back and had to retry

typedef enum { mode_stop, mode_hot, mode_won } mode_e;          // The various modes
mode_e quizMode = mode_stop;
int wonNode = -1;
bool tell2stop = false;               // when one node has won we need to tell the others to stop
int tell2cmd = -1;                    // tell all nodes what to do (from API)
int pingval = -1; 
int pingid = -1;

// command codes to nodes
#define CMD_STOP 0
#define CMD_HOT 1
#define CMD_WON 2
#define CMD_TEST 3

// class to define a custom server handler to handle files from the internal file system
// If a file exists, the handler returns true in canHandle()
// this handler is called by the server object on every GET to see if we have the file
class fileHandler : public RequestHandler {
  bool canHandle(HTTPMethod method, String uri) {
    return SPIFFS.exists(WebServer::urlDecode(uri));
  }
  bool handle(WebServer& server, HTTPMethod requestMethod, String requestUri) {   
    requestUri = WebServer::urlDecode(requestUri);
    // Serial.print("Handle " + requestUri + "type=");
    String contentType;
    contentType = getContentType(requestUri);
    // Serial.println(contentType);
    File rFile = SPIFFS.open(requestUri);
    
    if (rFile) {
      int fsizeFile = rFile.size();
      // Serial.printf("Filesize: %d\n", fsizeFile);
      server.sendHeader("Content-Length", (String)(fsizeFile));
      server.sendHeader("Cache-Control", "max-age=86400, public"); // cache for 24 hours
      server.streamFile(rFile, contentType);
      rFile.close();
    } else {
      server.send(500, "text/plain", "Internal error - can't open file");
    }
    return true;
  }
  String getContentType(String filename) {
    // from https://developer.mozilla.org/en-US/docs/Web/HTTP/Basics_of_HTTP/MIME_types/Common_types
    if (filename.endsWith(".htm")) {
      return "text/html";
    } else if (filename.endsWith(".html")) {
      return "text/html";
    } else if (filename.endsWith(".css")) {
      return "text/css";
    } else if (filename.endsWith(".js")) {
      return "application/javascript";
    } else if (filename.endsWith(".png")) {
      return "image/png";
    } else if (filename.endsWith(".gif")) {
      return "image/gif";
    } else if (filename.endsWith(".jpg")) {
      return "image/jpeg";
    } else if (filename.endsWith(".ico")) {
      return "image/x-icon";
    } else if (filename.endsWith(".xml")) {
      return "text/xml";
    } else if (filename.endsWith(".pdf")) {
      return "application/x-pdf";
    } else if (filename.endsWith(".zip")) {
      return "application/x-zip";
    } else if (filename.endsWith(".gz")) {
      return "application/x-gzip";
    } else if (filename.endsWith(".mp3")) {
      return "audio/mpeg";
    } else if (filename.endsWith(".mpeg")) {
      return "audio/mpeg";
    } else if (filename.endsWith(".wav")) {
      return "audio/wav";
    } else if (filename.endsWith(".aac")) {
      return "audio/aac";
    }
    return "text/plain";
  }

} fileHandler;


void setup(void) {
  pinMode(LEDBLUEPIN, OUTPUT);
  pinMode(LEDREDPIN, OUTPUT);
  pinMode(LEDGREENPIN, OUTPUT);
  digitalWrite(LEDBLUEPIN, LEDON); // turn on blue LED
  digitalWrite(LEDREDPIN, LEDOFF);
  digitalWrite(LEDGREENPIN, LEDOFF);
  pinMode(WIFIPORTALPIN, INPUT_PULLUP);
  pinMode(PIN_HOT,   INPUT_PULLUP);
  pinMode(PIN_STOP,  INPUT_PULLUP);
  pinMode(PIN_STATUS,INPUT_PULLUP);
  Serial.begin(115200);

  Serial.println();
  Serial.println("Quiz hub");

  // mount file system
 
  if (!SPIFFS.begin(false)) {
    Serial.println("File system not mounted, maybe we forgot to format/upload so we do try that");
    if (!SPIFFS.begin(true)) {
      Serial.println("Failure to mount file system - stop");
      return;
    }
  }

  // test code, see if we uploaded all required files in the file system flash
  displayDir(String("/sounds"));

  WiFi.setHostname(hostname);   
  // define relevant captive portal settings
  wm.setClass("invert");                // set dark theme
  wm.setConfigPortalTimeout(40);        // auto close configportal after n seconds

  if (digitalRead(WIFIPORTALPIN)==SWITCH_ON) {
    Serial.println("Button pressed at boot, wipe WiFi settings");
    wm.resetSettings(); // wipe settings to force 
  }

  WiFi.mode(WIFI_STA);

  
  // connect to stored Wifi settings or start captive portal if not succesful
  bool res;
  res = wm.autoConnect(ssid, password); // SSID and password in case 

  Serial.print("Now start Access point with SSID: ");
  Serial.println(ssid);
  // The following statement Wifi.softAP() does not work when WiFiManager just enabled its own Access point
  // this may be related to https://github.com/esp8266/Arduino/issues/3793
  // not sure. Only work around is to reboot one more time
  // another work around may be to use the webportal instead of the configportal in WiFiManager
  res = WiFi.softAP(ssid, password);      
  Serial.println(res);
    
  myIP = WiFi.softAPIP();
  Serial.print("My IP address: ");
  Serial.println(myIP);

  if(!res) {
    Serial.println("Failed to connect or hit timeout - stand alone AP mode");
  } else {
    Serial.print("Connected to selected WiFi: ");
    Serial.println(wm.getWiFiSSID());
    Serial.print("Local IP address: ");
    Serial.println(WiFi.localIP());
    Serial.printf("\nSTA: %s (dns: %s / %s)\n", WiFi.localIP().toString().c_str(), WiFi.dnsIP(0).toString().c_str(), WiFi.dnsIP(1).toString().c_str());

#ifdef NAT_SUPPORT
    // now we start the NAT service
    ip_napt_init(IP_NAPT_MAX, IP_PORTMAP_MAX);
    // u32_t napt_netif_ip = 0xC0A80401; // Set to ip address of softAP netif (Default is 192.168.4.1)
    // ip_napt_enable(htonl(napt_netif_ip), 1);
    ip_napt_enable_no(SOFTAP_IF, 1);
    Serial.printf("WiFi Network '%s' is now NATed behind '%s'\n", ssid, wm.getWiFiSSID().c_str());

#endif

  } 
  if (MDNS.begin(hostname)) {
    Serial.println(String("MDNS started and responds to http://")+hostname+".local");
  }

  digitalWrite(LEDBLUEPIN, LEDOFF); // turn off LED


  // configure standard Quiz web server
  server.on("/", handleRoot);
  server.on("/quiz", handleNodeAPI);
  server.on("/data", handleUIAPI);
  server.on("/getsounds", handleSoundAPI);
  server.addHandler(&fileHandler);  // handle any file within the storage area
  server.onNotFound(handleNotFound);
  server.enableCORS(true);          // allow APIs to be called accross domains
  server.begin();
  Serial.println("HTTP server for Quiz started");
}

void loop(void) {
  static unsigned long flashtime = millis();    // for hardware switch stabilization
  static bool sw=true;

  if (tell2stop) {
    tell2stop = false;
    // Serial.printf("Send stop to all except winner %d\n", wonNode);
    for (int i=0; i<MAXNODES; i++) {
      if (i!=wonNode) sendNode(i, CMD_STOP);
    }
  }
  
  if (((digitalRead(PIN_STOP)==SWITCH_ON) && flashtime+1000 < millis()) || tell2cmd==0) {
    Serial.println(F("Setting stop mode.."));
    digitalWrite(LEDREDPIN, LEDOFF);
    digitalWrite(LEDGREENPIN, LEDOFF);
    sendAllNodes(CMD_STOP);
    wonNode = -1;
    tell2cmd = -1;
    quizMode = mode_stop;
    flashtime = millis();
  } 
  
  if (((digitalRead(PIN_HOT)==SWITCH_ON) && flashtime+1000 < millis()) || tell2cmd==1) {
    Serial.println(F("Setting hot mode.."));
    sendAllNodes(CMD_HOT); // set hot to all buttons
    quizMode = mode_hot;
    digitalWrite(LEDREDPIN, LEDON);
    digitalWrite(LEDGREENPIN, LEDOFF);
    wonNode = -1;
    tell2cmd = -1;
    flashtime = millis();
  }

  if (((digitalRead(PIN_STATUS)==SWITCH_ON) && flashtime+1000 < millis()) || tell2cmd==2) {
    flashtime = millis();
    digitalWrite(LEDGREENPIN, LEDOFF);
    digitalWrite(LEDREDPIN, LEDOFF);
    displaystats();
    if (sw || tell2cmd==2) sendAllNodes(CMD_WON); else  sendAllNodes(CMD_STOP);
    sw = !sw;
    tell2cmd = -1;
  }

  if (pingval>=0 && pingid>=0) {
    Serial.printf("Send ping value %d to node %d\n", pingval, pingid);
    sendNode(pingid, pingval);
    pingval = -1;
    pingid = -1;
  }
  // normal processing
  server.handleClient();
  // MDNS.update();   // !!! This is an esp8266 library call that does not exists in ESP32
}

void displaystats() {
  byte i;
  // first display through serial port
  Serial.printf("Packets sent: %d\r\n", (unsigned long)packets);
  for (i=0; i<MAXNODES; i++) {
    if (nodeActive[i]>0)
      Serial.printf("%d (%s) - batt=%d wins=%d lateWins=%d, errHot=%d errStop=%d errComms=%d errMode=%d\r\n", i, 
          IPNode[i].toString().c_str(), battValue[i], wins[i], lateWins[i], errorsHot[i], errorsStop[i], 
          errorsAcc[i], errorsNode[i]);
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

void handleNodeAPI() {
  // reacts to /quiz and return json {"nodeID": 1,"mode": 0, "won": 0, "err":0}
  // if mode==mode_hot we declare a winner
  uint8_t i;
  int  curNode=-1;
  const size_t capacity = JSON_OBJECT_SIZE(4) + 61;
  StaticJsonDocument<capacity> doc;
  IPAddress rIP = server.client().remoteIP();
  digitalWrite(LEDBLUEPIN, LEDON); // turn on
  doc["err"] = 0;
  // Serial.print("client IP= ");
  // Serial.println(rIP);
  // first we look if we have the IP address of this node already
  for (i=0; i<nodeCnt; i++) {
    if (IPNode[i]==rIP) curNode=i;
  }
  if (curNode<0) {
    if (nodeCnt+1>=MAXNODES) {
      Serial.println("Error: too many nodes");
      doc["err"] = 1;
    } else {
      curNode = nodeCnt;  
      IPNode[curNode] = rIP;
      nodeActive[curNode] = 1;
      nodeCnt++;
    }
  }
  if (curNode>=0) {
    if (quizMode == mode_hot) {
      // Serial.printf("Node %d is winner\n", curNode);    
      quizMode = mode_won;
      wonNode = curNode;
      wins[curNode]++;
      tell2stop = true; // need to tell others to stop
      digitalWrite(LEDGREENPIN, LEDON);
      digitalWrite(LEDREDPIN, LEDOFF);
    } else {
      // Serial.printf("Node %d press\n", curNode);
      if (curNode == wonNode && quizMode == mode_won) lateWins[curNode]++;
    }
    doc["nodeID"] = curNode;
    doc["mode"] = quizMode;
    doc["won"] = (quizMode == mode_won && curNode == wonNode ? 1 : 0);
/*
    Serial.printf("# args = %d\n", server.args());
    for (i = 0; i < server.args(); i++) {
      Serial.printf("=> %s: %s\n", server.argName(i).c_str(), server.arg(i).c_str());
    }
*/
  }

  char output[200];
  serializeJson(doc, output);

  // Serial.printf("Sending: %s\n ", output);
  server.send(200, "application/json", output);
  digitalWrite(LEDBLUEPIN, LEDOFF); // turn off
}

void handleUIAPI() {
  // handles API calls from the UI client (index.html file). 
  //  http://quiz.local/data?cmd=val
  //   cmd=0 = stop, cmd=1 = hot, cmd=2 = win all, ..
  //   When cmd is not present: just get status
  // returns: {"mode": 1,"nodeCnt": 4, "NodeWon": 2, "stats":
  //    [ {"id":0, "ip":192.168.4.2", "battValue":3210, "wins":4, "errorsAcc":0, "errorsHot": 0, "errorsStop":0, "errorsNode":0, "lateWins":0},
  //      {"id":1, "ip":192.168.4.3", "battValue":3210, "wins":1, "errorsAcc":0, "errorsHot": 0, "errorsStop":0, "errorsNode":0, "lateWins":0},
  //    ]}
  const size_t capacity = JSON_ARRAY_SIZE(MAXNODES) + JSON_OBJECT_SIZE(5) + MAXNODES*JSON_OBJECT_SIZE(9) + 472; // see https://arduinojson.org/v6/assistant/
  StaticJsonDocument<capacity> doc;
  digitalWrite(LEDBLUEPIN, LEDON); // turn on LED

  for (int i = 0; i < server.args(); i++) {
    // Serial.printf("=> %s: %s\n", server.argName(i).c_str(), server.arg(i).c_str());
    if (server.argName(i)=="cmd") {
      tell2cmd = (mode_e) server.arg(i).toInt();
    }
    if (server.argName(i)=="ping") pingval = server.arg(i).toInt();
    if (server.argName(i)=="id") pingid = server.arg(i).toInt();
  }
//  Serial.print("Client IP=");
//  Serial.println(server.client().remoteIP().toString());
//  Serial.print("Server IP=");
//  Serial.println(server.client().localIP().toString());
  
  doc["mode"]= quizMode;
  doc["nodeCnt"]= nodeCnt;
  doc["ip"] = server.client().localIP().toString();
  int t = wonNode;
  doc["nodeWon"]= (quizMode==mode_won ? t : -1);
  for (byte i=0; i<MAXNODES; i++) if (nodeActive[i]>0) {
    doc["stats"][i]["id"]         = i;
    doc["stats"][i]["battValue"]  = battValue[i];
    doc["stats"][i]["ip"]         = IPNode[i].toString();
    doc["stats"][i]["wins"]       = wins[i];
    doc["stats"][i]["errorsAcc"]  = errorsAcc[i]; 
    doc["stats"][i]["errorsHot"]  = errorsHot[i]; 
    doc["stats"][i]["errorsStop"] = errorsStop[i]; 
    doc["stats"][i]["errorsNode"] = errorsNode[i]; 
    doc["stats"][i]["lateWins"]   = lateWins[i]; 
  }
  char output[700];
  serializeJson(doc, output);

  // Serial.printf("Sending: %s\n", output);
  server.send(200, "application/json", output);
  digitalWrite(LEDBLUEPIN, LEDOFF); // turn off LED
}

#define MAXSOUNDFILE 30

void handleSoundAPI() {
  // handles API calls from the UI client (index.html file). 
  //  http://quiz.local/getsounds
  // returns all file names from the sounds subdirectory of the file system:
  // {sounds:["name1.mp3", "name2.mp3", ...]} 
  
  const size_t capacity = JSON_ARRAY_SIZE(MAXSOUNDFILE) + JSON_OBJECT_SIZE(1) + 32; // see https://arduinojson.org/v6/assistant/
  StaticJsonDocument<capacity> doc;
  int n=0;
  Serial.println("Getsounds API");
  File dir = SPIFFS.open("/sounds");
  if (dir.isDirectory()) {
    File file = dir.openNextFile();
    while (file) {
      Serial.println(file.name());
      if (file.size()>0) {
         doc["sounds"][n] = String(file.name());
         n++;
      }
      file = dir.openNextFile();
    }
  }
  Serial.println(n);
  if (n==0) doc["sounds"] = "";
  
  char output[600];
  serializeJson(doc, output);

  // Serial.printf("Sending: %s\n", output);
  server.send(200, "application/json", output);
  
}

void sendAllNodes(byte cmd) {
  // Serial.printf("Send cmd %d to all nodes\n", cmd);
  for (uint8_t i=0; i<MAXNODES; i++) {
    sendNode(i, cmd);
  }
}

void sendNode( byte node, byte cmd) {
  // should return {"batt": 3000, "errCnt": 0, "btn": 0, "led": 0}
  if (nodeActive[node]>0) {
    // Serial.printf("%d %s\n", node, IPNode[node].toString().c_str());      
    String htmltxt = callurl("http://" + IPNode[node].toString() + "/quiz?cmd=" + cmd);
    const size_t capacity = JSON_OBJECT_SIZE(4) + 61; // see https://arduinojson.org/v6/assistant/
    StaticJsonDocument<capacity> doc;
    // Parse JSON object
    DeserializationError error = deserializeJson(doc, htmltxt);
    if (error) {
      Serial.printf("Json deserializeJson() failed: %s ", error.c_str());
      errorsAcc[node]++;
      return;
    }
    if (doc.containsKey("batt")) {
      // we probably have a right json object returned
      battValue[node] = doc["batt"];
      errorsNode[node] = doc["errCnt"];
      // don't do anything yet with doc["btn"]and doc["led"]
    } else {
      // incorrect json or none at all
      if (cmd==mode_stop) errorsStop[node]++;
      if (cmd==mode_hot) errorsHot[node]++;
      Serial.println("Invalid json returned");
    }
  }
}
void handleRoot() {
  digitalWrite(LEDBLUEPIN, LEDON);
  Serial.print("Root call from ");
  Serial.print(server.client().remoteIP());
  Serial.print(" server= ");
  String serverip = server.client().localIP().toString();
  Serial.println(serverip);
  server.send(200, "text/html", "<html>Welcome to the Quiz server! Start <a href='http://"+ serverip +"/index.html'>here</a></html>");
  delay(100);
  digitalWrite(LEDBLUEPIN, LEDOFF);
}

void handleNotFound() {
  digitalWrite(LEDBLUEPIN, LEDON);
  String message = "File Not Found\n\n";
  message += "Quiz URI: ";
  message += server.uri();
  Serial.println(message);
  message += "\nMethod: ";
  message += (server.method() == HTTP_GET) ? "GET" : "POST";
  message += "\nArguments: ";
  message += server.args();
  message += "\n";
  for (uint8_t i = 0; i < server.args(); i++) {
    message += " " + server.argName(i) + ": " + server.arg(i) + "\n";
  }
  server.send(404, "text/plain", message);
  digitalWrite(LEDBLUEPIN, LEDOFF);
}

void displayDir(String dirname) {
  // display a directory of the file system 
  File root = SPIFFS.open(dirname);
  Serial.println(dirname);
  if (!root) {
    Serial.println("- failed to open directory");
    return;
  }
  if (!root.isDirectory()) {
    Serial.println(" - not a directory");
    return;
  }

  File file = root.openNextFile();
  while (file) {
    Serial.print(file.name());
    Serial.print("\t ");
    Serial.println(file.size());
    file = root.openNextFile();
  }
}
