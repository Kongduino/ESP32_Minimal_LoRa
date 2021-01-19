# ESP32_Minimal_LoRa

The ESP32 (sub)version of [BastWAN_Minimal_LoRa](https://github.com/Kongduino/BastWAN_Minimal_LoRa). All the EEPROM and BME680 features have been removed, the idea here was to make it work as quickly as possible. I will re-integrate functionalities later.

Tested with an old Heltec Wifi LoRa 32 (the 433 MHz version) with an 868 MHz antenna. :-) works!

# IMPORTANT

## LoRa library

I am using a customized version of the LoRa library. The change is easy to do but has to be done every time you upgrade it:

```c
  uint8_t readRegister(uint8_t address);
  void writeRegister(uint8_t address, uint8_t value);
```

The declarations of these 2 functions are private, and need to be moved to public. The code needs access to these functions. And honestly there's no good reason for these 2 functions to be private...


![ESP32LoRa](ESP32LoRa.jpg)