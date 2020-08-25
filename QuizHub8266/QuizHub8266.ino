/*
Quiz hub code based on ESP 8266. The hub code can only talk with Quiz hub ESP8266 nodes

Will run Access Point with SSID of quizhub that runs a web server for:
- communication with the Quiz Nodes (buttons)
- handle config start/stop commands from a generic web browser (PC or mobile phone) to operate the quiz server.
  The web browser does not need to know the IP address and can go to the URL: http://quiz.local to get to the server

During boot operation we will also try to connect to a public WiFi access point. If this is succesfull a NAT router is setup
to this public WiFi access point, so all clinets of our own Access Point can also get to the internet.
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

We also use LittleFS to define a fie system to store files we need in the html interface. Things like html, css, js files and
images. The files are in the data folder of the Arduino sketch and you need to install the LiteFS data upload tool to upload
the data folder to the internal flash memory of the ESP8266. The upload tool, once installed, will show up in the Tools menu
of your Arduino environment as extra menu item "ESP8266 LittleFS Data Upload". See these links:
- https://arduino-esp8266.readthedocs.io/en/latest/filesystem.html
- https://github.com/earlephilhower/arduino-esp8266littlefs-plugin/releases

Still todo:
- If the server is rebooted the clients do not automatically reconnect (Wifi issue)
- index.html:
  - start/stop bottons
  - show stats screen
  - call the API: once every second or so
  - create test

Features to add:
  - Store team names somewhere (maybe in local storage in vue.js)
  - counting backwards (10 secs to answer)
  - 7 segment display?

*/

// Core Arduino ESP8266 libraries used
#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <ESP8266HTTPClient.h>
#include <LittleFS.h>  
#include <lwip/napt.h>
#include <lwip/dns.h>
#include <dhcpserver.h>

// Additional custom libraries used (installed through Library Manager or from Github)
#include <WiFiManager.h>    // https://github.com/tzapu/WiFiManager captive portal to connect to WiFi
#include <ArduinoJson.h>    // use arduinojson library to generate API structures


// ---------------------- configuration parameters -------------------------
#define QUIZSERVER "quiz"   // the name on the local network of our quiz hub server will be http://quiz.local
#define APSSID "quizhub"    // SSID of our advertised access point
#define APPSK  ""           // Password of access point (empty is none)

// Hardware PIN connections
#define LEDPIN 2          // blue led 0=on, 1=off
#define PIN_HOT 5      // enter hot mode to all nodes GPIO5 => D1 mark
#define PIN_STOP 4      // set stop mode to all nodes GPIO4 => D2
#define PIN_STATUS 0    // show statistics of all nodes and run connection test (will turn on/off all button LEDs) GPIO0 => D3
#define WIFIPORTALPIN PIN_HOT   // trigger WiFi portal during reboot 

// define NAT port ranges
#define NAPT 1000
#define NAPT_PORT 10

// We have 2 webservers defined on port 80, of which only 1 runs at a time
ESP8266WebServer server(80);    // web server for normal Quiz function
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
    return LittleFS.exists(ESP8266WebServer::urlDecode(uri));
  }
  bool handle(ESP8266WebServer& server, HTTPMethod requestMethod, String requestUri) {   
    requestUri = ESP8266WebServer::urlDecode(requestUri);
    Serial.print("Handle " + requestUri + "type=");
    String contentType;
    contentType = mime::getContentType(requestUri);
    Serial.println(contentType);
    File rFile = LittleFS.open(requestUri, "r");
    
    if (rFile) {
      int fsizeFile = rFile.size();
      Serial.printf("Filesize: %d\n", fsizeFile);
      server.sendHeader("Content-Length", (String)(fsizeFile));
      server.sendHeader("Cache-Control", "max-age=86400, public"); // cache for 24 hours
      server.streamFile(rFile, contentType);
      rFile.close();
    } else {
      server.send(500, "text/plain", "Internal error - can't open file");
    }
    return true;
  }
} fileHandler;


void setup(void) {
  pinMode(LEDPIN, OUTPUT);
  digitalWrite(LEDPIN, 0); // turn on LED
  pinMode(WIFIPORTALPIN, INPUT_PULLUP);
  pinMode(PIN_HOT,   INPUT_PULLUP);
  pinMode(PIN_STOP,  INPUT_PULLUP);
  pinMode(PIN_STATUS,INPUT_PULLUP);
  Serial.begin(115200);

  Serial.println();
  Serial.println("Quiz hub");

  // mount file system
  if (LittleFS.begin()) Serial.println("File system mounted. Files:");

  // test code, see if we uploaded all required files in the file system flash
  displayDir(String("/sounds"));

  WiFi.hostname(hostname);
  // define relevant captive portal settings
  wm.setClass("invert");                // set dark theme
  wm.setConfigPortalTimeout(40);        // auto close configportal after n seconds

  if (digitalRead(WIFIPORTALPIN) == LOW) {
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
 
    // now we start the NAT service
    // first give DNS servers to AP side
    dhcps_set_dns(0, WiFi.dnsIP(0));
    dhcps_set_dns(1, WiFi.dnsIP(1));
    // init NAT
    err_t ret = ip_napt_init(NAPT, NAPT_PORT);
    Serial.printf("ip_napt_init(%d,%d): ret=%d (OK=%d)\n", NAPT, NAPT_PORT, (int)ret, (int)ERR_OK);
    if (ret == ERR_OK) {
      ret = ip_napt_enable_no(SOFTAP_IF, 1);
      Serial.printf("ip_napt_enable_no(SOFTAP_IF): ret=%d (OK=%d)\n", (int)ret, (int)ERR_OK);
      if (ret == ERR_OK) {
        Serial.printf("WiFi Network '%s' is now NATed behind '%s'\n", ssid, wm.getWiFiSSID().c_str());
      }
    }
    if (ret != ERR_OK) {
      Serial.printf("NAT initialization failed\n");
    }
  } 
  if (MDNS.begin(hostname)) {
    Serial.println(String("MDNS started and responds to http://")+hostname+".local");
  }

  digitalWrite(LEDPIN, 1); // turn off LED


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
    Serial.printf("Send stop to all except winner %d\n", wonNode);
    for (int i=0; i<MAXNODES; i++) {
      if (i!=wonNode) sendNode(i, CMD_STOP);
    }
  }
  
  if ((!digitalRead(PIN_STOP) && flashtime+1000 < millis()) || tell2cmd==0) {
    Serial.println(F("Setting stop mode.."));
    sendAllNodes(CMD_STOP);
    wonNode = -1;
    tell2cmd = -1;
    quizMode = mode_stop;
    flashtime = millis();
  } 
  
  if ((!digitalRead(PIN_HOT) && flashtime+1000 < millis()) || tell2cmd==1) {
    Serial.println(F("Setting hot mode.."));
    sendAllNodes(CMD_HOT); // set hot to all buttons
    quizMode = mode_hot;
    wonNode = -1;
    tell2cmd = -1;
    flashtime = millis();
  }

  if ((!digitalRead(PIN_STATUS) && flashtime+1000 < millis()) || tell2cmd==2) {
    flashtime = millis();
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
  MDNS.update();  
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
  digitalWrite(LEDPIN, 0); // turn on
  doc["err"] = 0;
  Serial.print("client IP= ");
  Serial.println(rIP);
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
      Serial.printf("Node %d is winner\n", curNode);    
      quizMode = mode_won;
      wonNode = curNode;
      wins[curNode]++;
      tell2stop = true; // need to tell others to stop
    } else {
      Serial.printf("Node %d press\n", curNode);
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

  Serial.printf("Sending: %s\n ", output);
  server.send(200, "application/json", output);
  digitalWrite(LEDPIN, 1); // turn off
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

  Serial.printf("Sending: %s\n", output);
  server.send(200, "application/json", output);
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
  Dir dir = LittleFS.openDir("/sounds");
  while (dir.next()) {
    if (dir.fileSize()) {
       doc["sounds"][n] = dir.fileName();
       n++;
    }
  }
  if (n==0) doc["sounds"] = "";
  
  char output[600];
  serializeJson(doc, output);

  Serial.printf("Sending: %s\n", output);
  server.send(200, "application/json", output);
  
}

void sendAllNodes(byte cmd) {
  Serial.printf("Send cmd %d to all nodes\n", cmd);
  for (uint8_t i=0; i<MAXNODES; i++) {
    sendNode(i, cmd);
  }
}

void sendNode( byte node, byte cmd) {
  // should return {"batt": 3000, "errCnt": 0, "btn": 0, "led": 0}
  if (nodeActive[node]>0) {
    Serial.printf("%d %s\n", node, IPNode[node].toString().c_str());      
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
  digitalWrite(LEDPIN, 0);
  Serial.println("Root");
  Serial.println(server.client().remoteIP());
  String serverip = server.client().localIP().toString();
  server.send(200, "text/html", "<html>Welcome to the Quiz server! Start <a href='http://"+ serverip +"/index.html'>here</a></html>");
  delay(100);
  digitalWrite(LEDPIN, 1);
}

void handleNotFound() {
  digitalWrite(LEDPIN, 0);
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
  digitalWrite(LEDPIN, 1);
}

void displayDir(String dirname) {
  // display the root directory of the file system (code to be removed)
  Dir dir = LittleFS.openDir(dirname);
  while (dir.next()) {
    Serial.print(dir.fileName());
    Serial.print(" ");
    if (dir.fileSize()) {
        File f = dir.openFile("r");
        Serial.println(f.size());
    } else Serial.println();
  }
}
