/*
   Derived from examples by J. Coliz <maniacbug@ymail.com>
   Quiz buttons - node code (one for each button)

   There are up to 5 quiz button nodes that can all talk with the quiz hub. The quiz hub will determine
   which quiz button is pressed first.
   The buttons use receive mode to:
   - stop the button (LED off, none of the buttons work)
   - hot button (the LED will start flashing)
   - won (the LED will turn on)
   - lost the button (the LED will go out - another person has pressed the button earlier)
   Ack protocol will send the battery voltage value in mV

   Send commands:
   - pressing will send the node number using the dedicated channel for the node
     The hub will return the ACK signal which is 0 when the node is first or contain the node ID when
     the winner is already known.
     
   - In case a send/receive error occurs of initial try, the next loop will try again
   - In case a send/receive error occurs ofVthe ACK, the next call will make the hub think there is a
     'late' press again, so the hub will return the node number and the node will still know it has won

*/

// BOARD: PRO MINI 3.3V V/ 8Mhz ATMEGA328 8Mhz

 
#include <SPI.h>
#include "RF24.h"

// You must include printf and run printf_begin() if you wish to use radio.printDetails();
#include "printf.h"

/****************** User Config ***************************/
/***      Set Button number (1..5)                      ***/
uint16_t node = 2;

/* Hardware configuration: Set up nRF24L01 radio on SPI bus plus pins 7 & 8 */
RF24 radio(7,8);

#define PIN_SW   5            // Quiz switch for candidate to press
#define PIN_LED  6            // LED within the switch to indicate results/status

#define PIN_BATT_SENSE A0     // battery voltage sensor pin


/**********************************************************/
                                                                           // Topology
byte addresses[][6] = {"0Quiz","1Quiz","2Quiz","3Quiz","4Quiz","5Quiz"};   // Radio addresses

typedef enum { mode_stop, mode_hot, mode_won, mode_lost, mode_test } mode_e;          // The various mode

// while in mode_hot status and candidates can press the button we flash the LED
#define FLASHON 100      // 100 ms on
#define FLASHOFF 1000    // 1000-100 = 900 ms off
#define PAYLOADSIZE 2    // payload size always 2 bytes

void setup(){

  pinMode(PIN_BATT_SENSE, INPUT);
  pinMode(PIN_LED, OUTPUT);
  pinMode(PIN_SW, INPUT_PULLUP);
  digitalWrite(PIN_LED, LOW);   // LED off
  analogReference(INTERNAL);
  int t = analogRead(PIN_BATT_SENSE); // settle analogreference value
  randomSeed(t + node*1000);      // initiate random generator based on node at battery voltage

  Serial.begin(115200);
  printf_begin(); // This is for initializing printf that is used by printDetails()
  Serial.print(F("RF24/QuizButton/Node "));
  Serial.println(node);
 
  // Setup and configure radio

  radio.begin();
  radio.setChannel(124);
  radio.setDataRate(RF24_250KBPS);
  radio.setPayloadSize(PAYLOADSIZE);
  
  radio.enableAckPayload();                     // Allow optional ack payloads
  radio.enableDynamicPayloads();                // Ack payloads are dynamic payloads
  
  radio.openWritingPipe(addresses[0]);          // Send to master using pipe 0
  radio.openReadingPipe(1,addresses[node]);     // Open a reading pipe on our address, pipe 1

  // set retry period. In case 2 buttons are pressed at exactly the same time we can have
  // a network collision and we need to retry a few ms later. the delay must be unique
  // for each node to prevent duplicating the collision. We set radio.setRetries() using a
  // random number in the setAckPayload() function 

  radio.setPALevel(RF24_PA_MIN); // PA_MIN is default

  radio.startListening();                       // Start listening  
  setAckPayload();
  radio.printDetails(); 
}

void loop(void) {
  static unsigned long flashtime = millis();
  static mode_e mode = mode_stop;
  static byte maxtry = 0;
  uint16_t gotData = 0;
  
  switch (mode) {
    case mode_stop:
        digitalWrite(PIN_LED, LOW);   // LED off
        break;
        
    case mode_hot:
        // flash LED
        if (millis()-flashtime < FLASHON) 
          digitalWrite(PIN_LED, HIGH);
        else if (millis()-flashtime < FLASHOFF)
          digitalWrite(PIN_LED, LOW);
        else flashtime = millis();
        
        // look if btn is pressed
        if (!digitalRead(PIN_SW)) {
          // btn pressed - send data
          Serial.println(F("Btn press ")); 

          mode = claimwin(mode);
        }
        break;

    case mode_won:
        digitalWrite(PIN_LED, HIGH);   // LED on
        break;
    
    case mode_lost:
        digitalWrite(PIN_LED, LOW);   // LED off
        break;

    case mode_test:
        // claim a random win after a random time defined in flashtime
        digitalWrite(PIN_LED, LOW);   // LED off
        if (millis() > flashtime && maxtry>0) {
          maxtry--;
          if (maxtry>0) mode=claimwin(mode);
        }
        break;
    
  }

  byte pipeNo=0;
  if (radio.available(&pipeNo)) {
    radio.read(&gotData, sizeof(gotData));
    printf("Pipe %d Cmd received = %d\r\n", pipeNo, gotData);
    switch (gotData) {
      case 0: 
          mode = mode_stop; break;
      case 1: 
          mode = mode_hot; break;
      case 2: 
          mode = mode_won; break;
      case 3: 
          mode = mode_test; 
          flashtime = millis() + 20*random(10,40); // random after 200 to 800 ms
          maxtry = 2;  // no more than 2 times try to win
          break;      
    }
    radio.flush_rx();
    setAckPayload(); // return current battery voltage on next call
  }
}

mode_e claimwin(mode_e ret) {
  // try to claim a win. Send our node and see which ack is returned
  uint16_t gotData;
  radio.stopListening();
  radio.flush_rx();
  if (radio.write(&node, PAYLOADSIZE)) { 
    if (radio.available()) {                      // expect ack with payload
      radio.read(&gotData, sizeof(gotData));
      // if we receive a 0 or ourselves, we have won
      ret = (gotData==node || gotData==0 ? mode_won : mode_lost);
      Serial.println(gotData); 
      radio.flush_rx();
    } else {
      Serial.println(F("No ack"));
    }
  } else {
    Serial.println(F("Error radio write"));
  }
  radio.startListening();                       // Start listening  
  setAckPayload(); // return current battery voltage on next call
  return ret;
}

void setAckPayload() {
  // Battery sense pin. Voltage of 1.528V gets value of 1003 -> factor 1.523
  unsigned long batt = analogRead(PIN_BATT_SENSE);   // read battery voltage
  batt = (batt*1.523);
  radio.writeAckPayload(1, &batt, PAYLOADSIZE);     // 2 bytes only! Pre-load an ack-paylod into the buffer for pipe 1  
  radio.setRetries(random(5,15), 15);          // new random retry period, max retries
  Serial.print(F("Battery = "));
  Serial.println(batt);
}
