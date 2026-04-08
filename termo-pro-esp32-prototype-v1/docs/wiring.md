Starting Module – Ignition Control

  | Signal Source | Component  | Value / Type  | Connected To          | Purpose               |
  | ------------- | ---------- | ------------- | --------------------- | --------------------- |
  | ESP32 GPIO21  | R3         | 1 kΩ          | Q1 Base               | Base current limiting |
  | Q1            | PN2222A    | NPN BJT       | Relay Coil (K1)       | Low-side switching    |
  | D3            | 1N4007     | Flyback diode | Across Relay Coil     | Back-EMF protection   |
  | K1            | SANYOU SRD | Relay         | Dry Contact Terminals | Load isolation        |


  
Intelligent Thermometer / Thermostat Module   

  | Source     | Component    | Value | Connected To | Purpose             |
  | ---------- | ------------ | ----- | ------------ | ------------------- |
  | 5V         | RV1          | 10 kΩ | LCD V0       | Contrast adjustment |
  | ESP32 GPIO | Q1 (PN2222A) | NPN   | LCD K(-)     | Backlight switching |
  
Sensors and Inputs

  | Device          | Connection      | ESP32 Pin    | Notes                  |
  | --------------- | --------------- | ------------ | ---------------------- |
  | DHT11           | DATA            | GPIO16       | Temperature / Humidity |
  | Photoresistor   | Voltage Divider | GPIO32 (ADC) | Light sensing          |
  | R2              | 10 kΩ           | GPIO32 → GND | Divider resistor       |
  | Push Button SW1 | Digital Input   | GPIO         | User input             |
  | Push Button SW2 | Digital Input   | GPIO         | User input             |
