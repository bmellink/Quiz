/*
   Quiz buttons - hub code
   https://github.com/bmellink/QuizButtons

   There are up to 5 quiz buttons that can all talk with the quiz hub. The quiz hub will determine
   which quiz button is pressed first.
   The buttons use receive mode to:
   - stop the button (LED off, none of the buttons work)
   - hot button (the LED will start flashing)
   - won (the LED will turn on)
   - lost the button (the LED will go out - another person has pressed the button earlier)

   Receive commands:
   - pressing will send the analog value of the A0 pin (which represents the battery voltage)

   Features:
   - sound
   - counter - answer within X seconds
   - 7 segment display - from  https://github.com/bremme/arduino-tm1637
 */

// Suppors all AVR based Arduino boards
// BOARD used in my setup: MINI 5V/ ATMEGA168 / 16 Mhz
 
#include <SPI.h>
#include "RF24.h"
#include "printf.h"
#include "SevenSegmentTM1637.h"

/* Hardware configuration: Set up nRF24L01 radio on SPI bus plus pins 7 & 8 */
RF24 radio(7,8); // CE, CSN

// pins:
// please note that pin 10 is used on most AVR boards by the SPI library for SS
// and can not be used for something else
#define PIN_HOT 3       // enter hot mode to all nodes
#define PIN_STOP 4      // set stop mode to all nodes
#define PIN_STATUS 5    // show statistics of all nodes and run connection test (will turn on/off all button LEDs)
#define PIN_TEST 6      // run random win test to all nodes (will fill statistics over time)
#define PIN_CLK7 A1     // CLK pin 7 segment display TM1637
#define PIN_DIO7 A2     // DIO pin 7 segment display TM1637

SevenSegmentTM1637 display(PIN_CLK7, PIN_DIO7);

/**********************************************************/
                                                                           // Topology
byte addresses[][6] = {"0Quiz","1Quiz","2Quiz","3Quiz","4Quiz","5Quiz"};   // Radio addresses
#define MAXNODES 5

// statistics
unsigned int packets;                 // number of packets sent to all nodes (excl ACKs)
bool nodeActive[MAXNODES];            // we were able to succesfully send to node, so it probably exists
unsigned int battValue[MAXNODES];     // battery voltage in mV
unsigned int wins[MAXNODES];          // number of wins
unsigned int errorsAcc[MAXNODES];     // number of times a send command did not get an ACK back
unsigned int errorsHot[MAXNODES];     // number of times a send CMD_HOT command returned an error
unsigned int errorsStop[MAXNODES];    // number of times a send CMD_STOP command returned an error
unsigned int lateWins[MAXNODES];      // number of times a winning node did not get its winning ACK back and had to retry

typedef enum { mode_stop, mode_hot, mode_won } mode_e;          // The various modes

#define PAYLOADSIZE 2     // use max payload size of 2 bytes (or one byte in case of byte)

// command codes to nodes
#define CMD_STOP 0
#define CMD_HOT 1
#define CMD_WON 2
#define CMD_TEST 3

void sendAllNodes(byte cmd, byte winner=0); // must declare on top due to optional arguments

void setup() {
  pinMode(PIN_HOT,   INPUT_PULLUP);
  pinMode(PIN_STOP,  INPUT_PULLUP);
  pinMode(PIN_STATUS,INPUT_PULLUP);
  pinMode(PIN_TEST,  INPUT_PULLUP);

  Serial.begin(115200);
  Serial.println(F("RF24/QuizButton/Hub"));
  printf_begin(); 
  
  display.begin();            // initializes the display
  display.setBacklight(100);  // set the brightness to 100 %
  display.print("INIT");      // display INIT on the display

  // Setup and configure radio
  radio.begin();
  radio.setChannel(124);
  radio.setDataRate(RF24_250KBPS);
  radio.setPayloadSize(PAYLOADSIZE);            // always 2 bytes packages
  radio.setRetries(5, 10);                      // retry delay and max reyties
  radio.setPALevel(RF24_PA_MIN);
  
  radio.enableAckPayload();                     // Allow optional ack payloads
  radio.enableDynamicPayloads();                // Ack payloads are dynamic payloads
  
  radio.openWritingPipe(addresses[1]);          // Send to nodes using pipe 0
  radio.openReadingPipe(1,addresses[0]);        // Open a reading pipe for all nodes on our address, pipe 1

  radio.startListening();                       // Start listening  
  setNodeWonAck(0);

  radio.printDetails(); 
}

void loop(void) {
  static unsigned long flashtime = millis();    // for hardware switch stabilization
  static mode_e mode = mode_stop;
  uint16_t node_won = 0;
  static bool sw=true;
  
  if (!digitalRead(PIN_STOP) && flashtime+1000 < millis()) {
    Serial.println(F("Setting stop mode.."));
    sendAllNodes(CMD_STOP);
    node_won = 0;
    mode = mode_stop;
    flashtime = millis();
    display.clear();
    display.print("STOP");
  } 
  
  if (!digitalRead(PIN_HOT) && flashtime+1000 < millis()) {
    Serial.println(F("Setting hot mode.."));
    sendAllNodes(CMD_HOT); // set hot to all buttons
    mode = mode_hot;
    node_won = 0;
    flashtime = millis();
  }

  if (!digitalRead(PIN_STATUS) && flashtime+1000 < millis()) {
    flashtime = millis();
    displaystats();
    if (sw) sendAllNodes(CMD_WON); else  sendAllNodes(CMD_STOP);
    sw = !sw;
  }

  if (!digitalRead(PIN_TEST) && flashtime+1500 < millis()) {
    Serial.print(F("Sending test mode to nodes: "));
    sendAllNodesTest(CMD_TEST); // set hot to all buttons, and expect random win
    mode = mode_hot;
    node_won = 0;
    flashtime = millis();
  }
  
  if (radio.available() && mode==mode_hot) {
    radio.read(&node_won, PAYLOADSIZE);
    Serial.print(F("Node win received = "));
    Serial.println(node_won);
    if (node_won>0) {
      mode=mode_won;
      wins[node_won-1]++;
      setNodeWonAck(node_won); // tell other nodes on next ack
      sendAllNodes(CMD_STOP, node_won); // and tell them also right away
      flashNode(node_won);
    }
  }

  if (radio.available() && mode!=mode_hot) {
    uint16_t dummy=0;
    radio.read(&dummy, PAYLOADSIZE);
    Serial.print(F("Late press from node "));
    Serial.println(dummy);
    if (dummy>0 && node_won==dummy) lateWins[dummy-1]++; 
    sendAllNodes(CMD_STOP, node_won); // and tell them also right away
    flashNode(node_won);
  }  
}

void flashNode(byte node) {
  display.clear();
  display.write(' ');
  display.write('-');
  display.write(node+0x30);
  display.write('-');
  display.blink();
}

#define DDEL 800

void displaystats() {
  byte i;
  // first display through serial port
  printf("Packets sent: %d\r\n", (unsigned long)packets);
  for (i=0; i<MAXNODES; i++) {
    if (nodeActive[i]>0)
      printf("%d - batt=%d wins=%d lateWins=%d, errHot=%d errStop=%d noacks=%d\r\n", i+1, 
             battValue[i], wins[i], lateWins[i], errorsHot[i], errorsStop[i], errorsAcc[i]);
  }
  delay(DDEL);
  // if we keep the switch pressed we go into test mode
  if (!digitalRead(PIN_STATUS)) {
    display.clear();
    display.print("TEST");
    return; // this is comm test mode
  }

  // show stats of active nodes as long as stats key is pressed short
  for (i=0; i<MAXNODES; i++) if (nodeActive[i]>0) {
    if (!digitalRead(PIN_STOP) || !digitalRead(PIN_HOT)) return;
    display.clear();
    display.write('N');
    display.write('=');
    display.write(i+0x31);
    delay(DDEL);
    display.clear();
    display.print("BATT");
    delay(DDEL);
    display.clear();
    display.print(battValue[i]);
    delay(DDEL);
    if (!digitalRead(PIN_STOP) || !digitalRead(PIN_HOT)) return;
  /*
    display.clear();
    display.print("WINS");
    delay(DDEL);
    display.clear();
    display.print(wins[i]);
    delay(DDEL);
  */
    display.clear();
    display.print("ERR");
    delay(DDEL);
    if (!digitalRead(PIN_STOP) || !digitalRead(PIN_HOT)) return;
    display.clear();
    display.print(errorsHot[i]);
    delay(DDEL);
    display.clear();
    display.print(errorsStop[i]);
    delay(DDEL);
  }
}

void setNodeWonAck(uint16_t node) {
  radio.writeAckPayload(1, &node, sizeof(node));      // Pre-load an ack-paylod into the buffer for pipe 1  
}

void sendAllNodes(byte cmd, byte winner) {
  byte n;
  uint16_t readBatt = 0;

  if (cmd==CMD_HOT) display.clear();
  radio.stopListening();
  packets++;
  for (n=0; n<MAXNODES; n++) {                   // Send to all nodes
    radio.openWritingPipe(addresses[n+1]);  
    uint16_t data = (winner==n+1 ? CMD_WON : cmd);
    if (radio.write(&data, PAYLOADSIZE)) { 
      nodeActive[n] = true;
      if (radio.available()) {                    // expect ack with payload
        radio.read(&readBatt, PAYLOADSIZE);
        battValue[n] = readBatt;
        printf("Node %d, batt value = %d\r\n", n+1, readBatt);
        radio.flush_rx();
        if (cmd==CMD_HOT) display.write(n+0x31);  // show active node
      } else {
        printf("Node %d, missing ack\r\n", n+1);   
        errorsAcc[n]++;    
      }
    } else {
      printf("Node %d send error\r\n", n+1);
      if (cmd==CMD_HOT) errorsHot[n]++; else errorsStop[n]++;
    }
    delay(10);
  }
  radio.startListening();                       // Start listening again
  setNodeWonAck(winner);                        // set ack correctly
}

void sendAllNodesTest(byte cmd) {
  byte i,n;
  uint16_t readBatt = 0;
  static byte startnode = 0;
  byte savenode = startnode;
  radio.stopListening();
  packets++;
  do {    // make sure we only send to active nodes and start equaly to each one
    startnode = (startnode+1) % MAXNODES;
  } while (!nodeActive[startnode] && savenode!=startnode);

  if (savenode==startnode) {
    Serial.print(F("no active nodes, activate nodes first."));
  }
  for (i=0; i<MAXNODES; i++) {                   // Send to all nodes
    n = (i+startnode) % MAXNODES;                // when in test mode we should not always start with node 1, so we loop
    if (nodeActive[n]>0) {                       // only send command to nodes we know are alive 
      Serial.print(n+1);
      radio.openWritingPipe(addresses[n+1]);  
      uint16_t data = cmd;
      if (radio.write(&data, PAYLOADSIZE)) { 
        if (radio.available()) {                      // expect ack with payload
          radio.read(&readBatt, PAYLOADSIZE);
          battValue[n] = readBatt;
          radio.flush_rx();
        } else {
          errorsAcc[n]++;    
        }
      } else {
        if (cmd==CMD_HOT) errorsHot[n]++; else errorsStop[n]++;
      }
      delay(10);
    }
  }
  Serial.println();
  radio.startListening();                       // Start listening again
  setNodeWonAck(0);         // set ack correctly
}
