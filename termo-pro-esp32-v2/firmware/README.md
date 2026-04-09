# Firmware Guide

## Software Setup

Development was done using Arduino IDE v2.

The project is compatible with other development environments, provided the required libraries are installed.

Required libraries:

- Adafruit Unified Sensor (v1.1.15) – by Adafruit  
- DHT Sensor Library (v1.4.6) – by Adafruit  
- LiquidCrystal (standard Arduino library for 4-bit / 8-bit mode)

Install all libraries before compiling the firmware.

---

# MAC Address Configuration (Required)

Both ESP32 boards must have their MAC addresses configured inside the firmware.

You must retrieve the MAC address from each board and place it in the corresponding file.

## How to Retrieve the MAC Address

Upload the following minimal sketch to each ESP32:

```cpp
#include <WiFi.h>

void setup() {
  Serial.begin(115200);
  WiFi.mode(WIFI_STA);
  delay(100);

  Serial.print("ESP32 Board MAC Address: ");
  Serial.println(WiFi.macAddress());
}

void loop() {}
```

Open the Serial Monitor (115200 baud).  
The board’s MAC address will be displayed.

Repeat this process for both ESP32 boards.

---

# Where to Place the MAC Addresses

Locate the array named:

receiverMAC

Then configure as follows:

- Thermostat ESP32 MAC → place inside `startingModule.ino`
- Starting Module ESP32 MAC → place inside `termostat.ino`

Each board must contain the MAC address of the other board.

Incorrect MAC configuration will prevent communication between modules.

---

# File Structure and Dependencies

## termostat.ino

The following files must be located in the same folder:

- termostat.h  
- termostat_variables.h  

If these files are missing or placed elsewhere, compilation will fail.

## startingModule.ino

This file has no external dependencies and can compile independently.

---

# Flashing
- After all steps above you can start to flash the .ino and .h on the esp32's, be careful to flash on the corect ones!

# Important Notes

- Ensure both ESP32 boards are flashed with the correct firmware.
- Verify that the correct board type is selected in Arduino IDE.
- Confirm that WiFi mode is properly initialized before communication.
- Always double-check MAC address placement before testing communication.
