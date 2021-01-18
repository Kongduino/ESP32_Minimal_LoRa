#undef max
#undef min
#include <string>
#include <vector>

using namespace std;
template class basic_string<char>; // https://github.com/esp8266/Arduino/issues/1136
// Required or the code won't compile!

/*
  namespace std _GLIBCXX_VISIBILITY(default) {
  _GLIBCXX_BEGIN_NAMESPACE_VERSION
  void __throw_bad_alloc() {}
  }
  NOTE!
  Add:
  namespace std _GLIBCXX_VISIBILITY(default) {
    _GLIBCXX_BEGIN_NAMESPACE_VERSION
    void __throw_length_error(char const*) {
    }
    void __throw_out_of_range(char const*) {
    }
    void __throw_logic_error(char const*) {
    }
  }
  to ~/Library/Arduino15/packages/arduino/tools/arm-none-eabi-gcc/4.8.3-2014q1/arm-none-eabi/include/c++/4.8.3/bits/basic_string.h
  Or the code won't compile.
  |====================================|
  |-----> normally not need with ESP32!|
  |====================================|
*/

#define REG_OCP 0x0B
#define REG_PA_CONFIG 0x09
#define REG_LNA 0x0c
#define REG_OP_MODE 0x01
#define REG_MODEM_CONFIG_1 0x1d
#define REG_MODEM_CONFIG_2 0x1e
#define REG_MODEM_CONFIG_3 0x26
#define REG_PA_DAC 0x4D
#define PA_DAC_HIGH 0x87
#define SS      18
#define RST     14
#define DI0     26
#define BAND 868125000

#define CBC 0
#define CTR 1
#define ECB 2

SSD1306  display(0x3c, 4, 15);
uint8_t myMode = ECB;
bool needEncryption = true;
bool pongBack = true;
uint8_t SecretKey[33] = "YELLOW SUBMARINEENIRAMBUS WOLLEY";
uint8_t encBuf[128], hexBuf[256], msgBuf[256];
uint8_t randomStock[256];
uint8_t randomIndex = 0;
float lastBattery = 0.0;
double batteryUpdateDelay;
char deviceName[33];
uint32_t myFreq = 868125000;
int mySF = 10;
uint8_t myBW = 8;
double BWs[10] = {
  7.8, 10.4, 15.6, 20.8, 31.25,
  41.7, 62.5, 125.0, 250.0, 500.0
};
uint16_t pingCounter = 0;

uint16_t encryptECB(uint8_t*);
void decryptECB(uint8_t*, uint8_t);
void array2hex(uint8_t *, size_t, uint8_t *, uint8_t);
void hex2array(uint8_t *, uint8_t *, size_t);
void sendPacket(char *);
void setPWD(char *);
void setPongBack(bool);
void stockUpRandom();
void showHelp();
uint8_t getRandomByte();
uint16_t getRamdom16();
void getRandomBytes(uint8_t *buff, uint8_t count);
void getBattery();
void setFQ(char*);
void setSF(char*);
void setBW(char* buff);
void setDeviceName(char *);
void sendJSONPacket();
void savePrefs();

void writeRegister(uint8_t reg, uint8_t value) {
  LoRa.writeRegister(reg, value);
}
uint8_t readRegister(uint8_t reg) {
  return LoRa.readRegister(reg);
}

void hex2array(uint8_t *src, uint8_t *dst, size_t sLen) {
  size_t i, n = 0;
  for (i = 0; i < sLen; i += 2) {
    uint8_t x, c;
    c = src[i];
    if (c != '-') {
      if (c > 0x39) c -= 55;
      else c -= 0x30;
      x = c << 4;
      c = src[i + 1];
      if (c > 0x39) c -= 55;
      else c -= 0x30;
      dst[n++] = (x + c);
    }
  }
}

void array2hex(uint8_t *inBuf, size_t sLen, uint8_t *outBuf, uint8_t dashFreq = 0) {
  size_t i, len, n = 0;
  const char * hex = "0123456789ABCDEF";
  for (i = 0; i < sLen; ++i) {
    outBuf[n++] = hex[(inBuf[i] >> 4) & 0xF];
    outBuf[n++] = hex[inBuf[i] & 0xF];
    if (dashFreq > 0 && i != sLen - 1) {
      if ((i + 1) % dashFreq == 0) outBuf[n++] = '-';
    }
  }
  outBuf[n++] = 0;
}

void setPWD(char *buff) {
  // buff can be 32 or 64 bytes:
  // 32 bytes = plain text
  // 64 bytes = hex-encoded
  uint8_t len = strlen(buff), i;
  for (i = 0; i < len; i++) {
    if (buff[i] < 32) {
      buff[i] = 0;
      i = len + 1;
    }
  }
  len = strlen(buff);
  Serial.print("setPWD: ");
  Serial.println(buff);
  Serial.print("len: ");
  Serial.println(len);
  hexDump((uint8_t *)buff, len);
  if (len == 32) {
    // copy to the SecretKey buffer
    memcpy(SecretKey, buff, 32);
    needEncryption = true;
    hexDump((uint8_t *)SecretKey, 32);
    return;
  }
  if (len == 64) {
    // copy to the SecretKey buffer
    hex2array((uint8_t *)buff, SecretKey, 64);
    needEncryption = true;
    hexDump((uint8_t *)SecretKey, 32);
    return;
  }
}

void sendPacket(char *buff) {
  LoRa.idle();
  LoRa.writeRegister(REG_LNA, 00); // TURN OFF LNA FOR TRANSMIT
  uint16_t olen = strlen(buff);
  memcpy(encBuf + 8, buff, olen);

  // prepend UUID
  // 4 bytes --> 8 bytes
  uint8_t ix = 0;
  getRandomBytes(encBuf, 4);
  //  encBuf[ix++] = getRandomByte();
  //  encBuf[ix++] = getRandomByte();
  //  encBuf[ix++] = getRandomByte();
  //  encBuf[ix++] = getRandomByte();
  array2hex(encBuf, 4, hexBuf);
  memcpy(encBuf, hexBuf, 8);

  olen += 8;
  Serial.println("Before calling encryption. olen = " + String(olen));
  memcpy(msgBuf, encBuf, olen);
  hexDump(msgBuf, olen);

  if (needEncryption) {
    olen = encryptECB((uint8_t*)msgBuf);
    // encBuff = encrypted buffer
    // hexBuff = encBuf, hex encoded
    // olen = len(hexBuf)
  } Serial.println("olen: " + String(olen));
  Serial.print("Sending packet...");
  // Now send a packet
  digitalWrite(LED_BUILTIN, 1);
  //digitalWrite(PIN_PA28, LOW);
  LoRa.beginPacket();
  if (needEncryption) {
    //LoRa.print((char*)hexBuf);
    LoRa.write(hexBuf, olen);
  } else {
    LoRa.write((uint8_t *)buff, olen);
  }
  LoRa.endPacket();
  //digitalWrite(PIN_PA28, HIGH);
  Serial.println(" done!");
  delay(500);
  digitalWrite(LED_BUILTIN, 0);
  LoRa.receive();
  LoRa.writeRegister(REG_LNA, 0x23); // TURN ON LNA FOR RECEIVE
}

void decryptECB(uint8_t* myBuf, uint8_t olen) {
  Serial.println(" . Decrypting:");
  hexDump(myBuf, olen);
  Serial.println("  - Dehexing myBuf to encBuf:");
  hex2array(myBuf, encBuf, olen);
  uint8_t len = olen / 2;
  hexDump(encBuf, len);
  Serial.println("  - Decrypting encBuf:");
  struct AES_ctx ctx;
  AES_init_ctx(&ctx, SecretKey);
  uint8_t rounds = len / 16, steps = 0;
  for (uint8_t ix = 0; ix < rounds; ix++) {
    //void AES_ECB_encrypt(const struct AES_ctx* ctx, uint8_t* buf);
    AES_ECB_decrypt(&ctx, (uint8_t*)encBuf + steps);
    steps += 16;
    // encrypts in place, 16 bytes at a time
  } encBuf[steps] = 0;
  hexDump(encBuf, len);
}

uint16_t encryptECB(uint8_t* myBuf) {
  // first ascertain length
  uint8_t len = strlen((char*)myBuf);
  uint16_t olen;
  struct AES_ctx ctx;
  // prepare the buffer
  olen = len;
  if (olen != 16) {
    if (olen % 16 > 0) {
      if (olen < 16) olen = 16;
      else olen += 16 - (olen % 16);
    }
  }
  Serial.println("myBuf:");
  Serial.print("[encryptECB]: ");
  Serial.print("olen = " + String(olen));
  Serial.println(", len = " + String(len));
  memset(encBuf, (olen - len), olen);
  memcpy(encBuf, myBuf, len);
  encBuf[len] = 0;
  hexDump(encBuf, olen);
  AES_init_ctx(&ctx, (const uint8_t*)SecretKey);
  uint8_t rounds = olen / 16, steps = 0;
  for (uint8_t ix = 0; ix < rounds; ix++) {
    AES_ECB_encrypt(&ctx, encBuf + steps);
    // void AES_ECB_decrypt(&ctx, encBuf + steps);
    steps += 16;
    // encrypts in place, 16 bytes at a time
  }
  array2hex(encBuf, olen, hexBuf);
  Serial.println("encBuf:");
  hexDump(encBuf, olen);
  Serial.println("hexBuf:");
  hexDump(hexBuf, olen * 2);
  return (olen * 2);
}

void stockUpRandom() {
  fillRandom(randomStock, 256);
  randomIndex = 0;
}

void showHelp() {
  Serial.print("--- HELP ---");
  Serial.println("\n D<max 32 chars> : Set device name");
  Serial.print("   -> right now  : "); Serial.println(deviceName);
  Serial.println("\n >xxxxxxxxxxx    : send string xxxxxxxxxxx");
  Serial.println("\n E               : turn on encryption");
  Serial.println(" e               : turn off encryption");
  Serial.print("   -> right now  : "); Serial.println(needEncryption ? "on" : "off");
  Serial.println("\n P<32 chars>     : set password [32 chars]");
  Serial.println("  [exactly 32]     (Uses AES256)");
  Serial.println("\n R               : turn on PONG back [Reply on]");
  Serial.println(" r               : turn off PONG back [reply off]");
  Serial.print("   -> right now  : "); Serial.println(pongBack ? "on" : "off");
  Serial.println("\n F<float>        : Set a new LoRa frequency.");
  Serial.println("  Between 862.0 and 1020.0 MHz (HF)");
  Serial.print("   -> right now  : "); Serial.println(myFreq / 1e6, 3);
  Serial.println("\n S[7-12]         : Set a new LoRa Spreading Factor.");
  Serial.print("   -> right now  : "); Serial.println(mySF);
  Serial.println("\n B[0-9]          : Set a new LoRa Bandwidth.");
  Serial.println("  From 0: 7.8 KHz to 9: 500 KHz");
  Serial.print("   -> right now  : ");
  Serial.print(myBW); Serial.print(": ");
  Serial.print(BWs[myBW]); Serial.println(" KHz");
  Serial.println("\n p               : send PING packet with counter & frequency");
  Serial.println("\n Anything else   : show this help message.");
}

void setPongBack(bool x) {
  pongBack = x;
  Serial.print("PONG back set to ");
  if (x) Serial.println("true");
  else Serial.println("false");
}
uint8_t getRandomByte() {
  uint8_t r = randomStock[randomIndex++];
  // reset random stock automatically if needed
  if (randomIndex > 254) stockUpRandom();
  return r;
}

uint16_t getRamdom16() {
  uint8_t r0 = randomStock[randomIndex++];
  // reset random stock automatically if needed
  if (randomIndex > 254) stockUpRandom();
  uint8_t r1 = randomStock[randomIndex++];
  // reset random stock automatically if needed
  if (randomIndex > 254) stockUpRandom();
  return r1 * 256 + r0;
}

void getRandomBytes(uint8_t *buff, uint8_t count) {
  uint8_t r;
  for (uint8_t i = 0; i < count; i++) {
    buff[i] = randomStock[randomIndex++];
    // reset random stock automatically if needed
    if (randomIndex > 254) stockUpRandom();
  }
}

void getBattery() {
  float battery = analogRead(A0);
  if (battery != lastBattery) {
    // update visually etc.
    Serial.println("Last Battery: " + String(lastBattery) + " vs current: " + String(battery));
    lastBattery = battery;
  }
}

void setFQ(char* buff) {
  uint32_t fq = (uint32_t)(atof(buff) * 1e6);
  // RAK4260: 862 to 1020 MHz frequency coverage
  // clearFrame();
  if (fq < 862e6 || fq > 1020e6) {
    Serial.println("Requested frequency (" + String(buff) + ") is invalid!");
  } else {
    myFreq = fq;
    LoRa.idle();
    LoRa.setFrequency(myFreq);
    delay(100);
    LoRa.receive();
    Serial.println("Frequency set to " + String(myFreq / 1e6, 3) + " MHz");
    savePrefs();
  }
}

void setSF(char* buff) {
  int sf = atoi(buff);
  // SF 7 to 12
  // clearFrame();
  if (sf < 7 || sf > 12) {
    Serial.println("Requested SF (" + String(buff) + ") is invalid!");
  } else {
    mySF = sf;
    LoRa.idle();
    LoRa.setSpreadingFactor(mySF);
    delay(100);
    LoRa.receive();
    Serial.println("SF set to " + String(mySF));
    savePrefs();
  }
}

void setBW(char* buff) {
  int bw = atoi(buff);
  /*Signal bandwidth:
    0000  7.8 kHz
    0001  10.4 kHz
    0010  15.6 kHz
    0011  20.8kHz
    0100  31.25 kHz
    0101  41.7 kHz
    0110  62.5 kHz
    0111  125 kHz
    1000  250 kHz
    1001  500 kHz
  */
  // clearFrame();
  if (bw < 0 || bw > 9) {
    Serial.println("Requested BW (" + String(bw) + ") is invalid!");
  } else {
    String s = "BW set to: " + String(bw);
    myBW = bw;
    LoRa.idle();
    LoRa.setSignalBandwidth(BWs[myBW] * 1e3);
    delay(100);
    LoRa.receive();
    Serial.println("BW set to " + String(BWs[myBW]));
    savePrefs();
  }
}

void setDeviceName(char *truc) {
  memset(deviceName, 0, 33);
  memcpy(deviceName, truc, strlen(truc));
  Serial.print("Device Name set to: ");
  Serial.println(deviceName);
  savePrefs();
}

void prepareJSONPacket(char *buff) {
  StaticJsonDocument<256> doc;
  memset(msgBuf, 0, 256);
  char myID[9];
  getRandomBytes(encBuf, 4);
  array2hex(encBuf, 4, (uint8_t*)myID);
  myID[8] = 0;
  doc["UUID"] = myID;
  doc["cmd"] = "msg";
  doc["msg"] = buff;
  doc["from"] = deviceName;
  serializeJson(doc, (char*)msgBuf, 256);
  sendJSONPacket();
}

void sendJSONPacket() {
  Serial.println("Sending JSON Packet... ");
  LoRa.idle();
  LoRa.writeRegister(REG_LNA, 00); // TURN OFF LNA FOR TRANSMIT
  uint16_t olen = strlen((char*)msgBuf);
  hexDump(msgBuf, olen);
  if (needEncryption) {
    olen = encryptECB((uint8_t*)msgBuf);
    // encBuff = encrypted buffer
    // hexBuff = encBuf, hex encoded
    // olen = len(hexBuf)
  } // Serial.println("olen: " + String(olen));
  Serial.print("Sending packet...");
  // Now send a packet
  digitalWrite(LED_BUILTIN, 1);
  //digitalWrite(PIN_PA28, LOW);
  LoRa.beginPacket();
  if (needEncryption) {
    //LoRa.print((char*)hexBuf);
    LoRa.write(hexBuf, olen);
  } else {
    //LoRa.print(buff);
    LoRa.write(msgBuf, olen);
  }
  LoRa.endPacket();
  /*
    RegRssiValue (0x1B)
    Current RSSI value (dBm)
    RSSI[dBm] = -157 + Rssi (using HF output port) or
    RSSI[dBm] = -164 + Rssi (using LF output port)
    Let's see if it has any meaning
  */
  Serial.println(" done!");
  delay(500);
  digitalWrite(LED_BUILTIN, 0);
  LoRa.receive();
  LoRa.writeRegister(REG_LNA, 0x23); // TURN ON LNA FOR RECEIVE
}

void sendPing() {
  // PING!
  StaticJsonDocument<256> doc;
  char myID[9];
  getRandomBytes(encBuf, 4);
  array2hex(encBuf, 4, (uint8_t*)myID);
  myID[8] = 0;
  doc["from"] = deviceName;
  doc["UUID"] = myID;
  doc["cmd"] = "ping";
  string fq, fk;
  Serial.print("myFreq: ");Serial.println(myFreq);
  String ff = String(myFreq * 1000);
  Serial.print("ff: ");Serial.println(ff);
  fk = ff.c_str();
  fq = fk.substr(0, 3);
  fq.append(".");
  fq.append(fk.substr(3, 3));
  fq.append(" MHz");
  Serial.print("fq: ");Serial.println(fq.c_str());
  doc["freq"] = fq.c_str();
  serializeJson(doc, (char*)msgBuf, 256);
  sendJSONPacket();
  Serial.println("PING sent!");
  delay(1000);
}

void sendPong(char *msgID, int rssi) {
  // PONG!
  StaticJsonDocument<256> doc;
  char myID[9];
  getRandomBytes(encBuf, 4);
  array2hex(encBuf, 4, (uint8_t*)myID);
  myID[8] = 0;
  doc["UUID"] = myID;
  doc["msgID"] = msgID;
  doc["cmd"] = "pong";
  doc["from"] = deviceName;
  doc["rcvRSSI"] = rssi;
  string fq, fk;
  String ff = String(myFreq * 1000);
  fk = ff.c_str();
  fq = fk.substr(0, 3);
  fq.append(".");
  fq.append(fk.substr(3, 3));
  fq.append(" MHz");
  doc["freq"] = fq.c_str();
  serializeJson(doc, (char*)msgBuf, 256);
  sendJSONPacket();
  Serial.println("PONG sent!");
  delay(1000);
}

void savePrefs() {
  //  Serial.println("Saving prefs:");
  //  StaticJsonDocument<200> doc;
  //  doc["myFreq"] = myFreq;
  //  doc["mySF"] = mySF;
  //  doc["myBW"] = myBW;
  //  doc["deviceName"] = deviceName;
  //  memset(msgBuf, 0, 97);
  //  serializeJson(doc, (char*)msgBuf, 97);
  //  hexDump(msgBuf, 96);
  //  myMem.write(0, msgBuf, 32);
  //  myMem.write(32, msgBuf + 32, 32);
  //  myMem.write(64, msgBuf + 64, 32);
}
