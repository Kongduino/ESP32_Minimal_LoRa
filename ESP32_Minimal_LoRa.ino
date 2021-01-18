#include <SPI.h>
#include "SSD1306.h" // alias for `#include "SSD1306Wire.h"`
#include <LoRa.h>
#include <LoRandom.h>
#include "aes.c"
#include "ArduinoJson.h"
// Click here to get the library: http://librarymanager/All#ArduinoJson

#include "helper.h"

void setup() {
  // ---- HOUSEKEEPING ----
  Serial.begin(115200);
  delay(3000);
  Serial.println("\n\nESP32 at your service!");
  pinMode(16, OUTPUT);
  digitalWrite(16, LOW);
  // set GPIO16 low to reset OLED
  delay(50);
  digitalWrite(16, HIGH);
  // while OLED is running, must set GPIO16 in high
  display.init();
  display.flipScreenVertically();
  display.setFont(ArialMT_Plain_10);
  display.drawString(0, 0, "\n\nOLED Inited");
  Serial.println("\n\nOLED Inited\nReceiver");
  SPI.begin(5, 19, 27, 18);
  LoRa.setPins(SS, RST, DI0);
  if (!LoRa.begin(BAND)) {
    Serial.println("Starting LoRa failed!");
    display.drawString(0, 20, "LoRa Init Fail");
    display.display();
    while (1);
  }
  stockUpRandom();
  // first fill a 256-byte array with random bytes
  LoRa.setSpreadingFactor(10);
  LoRa.setSignalBandwidth(250e3);
  LoRa.setCodingRate4(5);
  LoRa.setPreambleLength(8);
  LoRa.setTxPower(20, PA_OUTPUT_PA_BOOST_PIN);
  LoRa.writeRegister(REG_PA_CONFIG, 0b11111111); // That's for the transceiver
  // 0B 1111 1111
  // 1    PA_BOOST pin. Maximum power of +20 dBm
  // 111  MaxPower 10.8+0.6*MaxPower [dBm] = 15
  // 1111 OutputPower Pout=17-(15-OutputPower) if PaSelect = 1 --> 17
  LoRa.writeRegister(REG_PA_DAC, PA_DAC_HIGH); // That's for the transceiver
  // 0B 1000 0111
  // 00000 RESERVED
  // 111 +20dBm on PA_BOOST when OutputPower=1111
  //  LoRa.writeRegister(REG_LNA, 00); // TURN OFF LNA FOR TRANSMIT
  LoRa.writeRegister(REG_OCP, 0b00111111); // MAX OCP
  // 0b 0010 0011
  // 001 G1 = highest gain
  // 00  Default LNA current
  // 0   Reserved
  // 11  Boost on, 150% LNA current
  LoRa.receive();
  LoRa.writeRegister(REG_LNA, 0x23); // TURN ON LNA FOR RECEIVE
  setDeviceName("ESP32-LoRa");
}

void loop() {
  int packetSize = LoRa.parsePacket();
  if (packetSize) {
    memset(msgBuf, 0, 256);
    int ix = 0;
    while (LoRa.available()) {
      char c = (char)LoRa.read();
      delay(10);
      //if (c > 31)
      msgBuf[ix++] = c;
      // filter out non-printable chars (like 0x1A)
    } msgBuf[ix] = 0;
    Serial.print("Received packet: ");
    if (needEncryption) {
      Serial.println(" . Decrypting...");
      decryptECB(msgBuf, strlen((char*)msgBuf));
      memset(msgBuf, 0, 256);
      memcpy(msgBuf, encBuf, strlen((char*)encBuf));
    }
    StaticJsonDocument<200> doc;
    DeserializationError error = deserializeJson(doc, msgBuf);
    if (error) {
      Serial.print(F("deserializeJson() failed: "));
      Serial.println(error.f_str());
      return;
    }
    // Print 4-byte ID
    const char *myID = doc["UUID"];
    Serial.print("ID: ");
    Serial.println(myID);

    // Print sender
    const char *from = doc["from"];
    Serial.print("Sender: ");
    Serial.println(from);

    // Print command
    const char *cmd = doc["cmd"];
    Serial.print("Command: ");
    Serial.println(cmd);

    // Do we have a message?
    if (strcmp(cmd, "msg") == 0) {
      const char *msg = doc["msg"];
      Serial.print("Message: ");
      Serial.println(msg);
    }

    Serial.print("RSSI: ");
    int rssi = LoRa.packetRssi();
    Serial.println(rssi);

    if (strcmp(cmd, "ping") == 0 && pongBack) {
      // if it's a PING, and we are set to respond:
      LoRa.idle();
      Serial.println("Pong back:");
      // we cannot pong back right away – the message could be lost
      uint16_t dl = getRamdom16() % 2500 + 800;
      Serial.println("Delaying " + String(dl) + " millis...");
      delay(dl);
      sendPong((char*)myID, rssi);
      LoRa.receive();
    }
  }
  if (Serial.available()) {
    memset(msgBuf, 0, 256);
    int ix = 0;
    while (Serial.available()) {
      char c = Serial.read();
      delay(10);
      if (c > 31) msgBuf[ix++] = c;
    } msgBuf[ix] = 0;
    char c = msgBuf[0]; // Command
    if (c == '>') {
      char buff[256];
      strcpy(buff, (char*)msgBuf + 1);
      prepareJSONPacket(buff);
      sendJSONPacket();
    } else if (c == 'D') setDeviceName((char*)msgBuf + 1);
    else if (c == 'E') needEncryption = true;
    else if (c == 'e') needEncryption = false;
    else if (c == 'P') setPWD((char*)msgBuf + 1);
    else if (c == 'R') setPongBack(true);
    else if (c == 'r') setPongBack(false);
    else if (c == 'F') setFQ((char*)msgBuf + 1);
    else if (c == 'S') setSF((char*)msgBuf + 1);
    else if (c == 'B') setBW((char*)msgBuf + 1);
    else if (c == 'p') sendPing();
    else if (c == '/') {
      c = msgBuf[1]; // Subcommand
    } else {
      Serial.println((char*)msgBuf);
      showHelp();
    }
  }
}
