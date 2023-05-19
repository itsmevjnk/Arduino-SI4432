#include "si4432.h"

#define RESPONCE 1

Si4432 radio(10, 7, 2); // CS, SDN, IRQ

uint8_t radio_config[] = {
  0x89, 0x3C, 0x02,  // 0x1C-1E
  0xAB, 0x00, 0xBF, 0x26, 0x00, 0xB4,  // 0x20-25
  0xFF,  // 0x2A
  0xAD,  // 0x30
  0x0F, 0x66, 0x08, 0x38, 0x2D, 0xD4, 0x00, 0x00, 0x55, 0xAA, 0x55, 0xAA,  // 0x32-3D
  0x55, 0xAA, 0x55, 0xAA, 0xFF, 0xFF, 0xFF, 0xFF,  // 0x3F-46
  0x60,  // 0x69
  0x1F, 0x11, 0xEC, 0x00, 0x23, 0xF0,  // 0x6D-72
  0x53, 0x4A, 0xFF,  // 0x75-77
  0x03, 0x32 // 0x79-7A
};

void setup() {
  delay(1000);
  Serial.begin(115200);
  if (radio.init(radio_config) == false) {
    Serial.println("SI4432 not installed");
    while(1);
  }
  //radio.setBaudRate(70);
  //radio.setFrequency(433);
  radio.readAll();

  radio.startListening();

}

void loop() {
  byte txBuf[64];
  sprintf(txBuf,"Hello World %lu", millis());
  byte txLen = strlen((char *)txBuf);

  bool sent = radio.sendPacket(txLen, txBuf);
  //Serial.print("sent=");
  //Serial.println(sent);
  if (sent) {
    
#if RESPONCE
    byte rxLen;
    byte rxBuf[64];
    unsigned long startMillis = millis();
    Serial.println("startListening");
    radio.startListening(); // restart the listening.
    while(1) {
      bool recv = radio.isPacketReceived();
      if (recv) {
        radio.getPacketReceived(&rxLen, rxBuf);
        unsigned long receiveMillis = millis();
        unsigned long elaspedMills = receiveMillis - startMillis;
        Serial.print(receiveMillis);
        Serial.print(" rxLen:");
        Serial.print(rxLen, DEC);
        Serial.print(" elasped:");
        Serial.print(elaspedMills);
        Serial.println();
        
        for (int i = 0; i < rxLen; ++i) {
          Serial.print(rxBuf[i], HEX);
          Serial.print(" ");
        }
        Serial.println();
    
        for (int i = 0; i < txLen; ++i) {
          char c = txBuf[i];
          if (c > 0x20 && c < 0xFF) {
            Serial.print(c);
          } else {
            Serial.print(" ");
          }
        }
        Serial.print("-->");
        for (int i = 0; i < rxLen; ++i) {
          char c = rxBuf[i];
          if (c > 0x20 && c < 0xFF) {
            Serial.print(c);
          } else {
            Serial.print(" ");
          }
        }
        Serial.println();
        break;
      } // end if

      if ( (millis() - startMillis) > 1000) {
        Serial.println("No responce within 1000 ms");
        break;
      }
      
    } // end while
#endif

  } else {
    Serial.println("sendPacket Fail");
    delay(10000);
  }

  delay(1000);

}
