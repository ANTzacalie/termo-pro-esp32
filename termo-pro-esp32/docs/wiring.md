1. Starting Module (Ignition Control):

-ESP32 (GPIO21) -> Resistor R3 (1k) 
-Resistor R3 (1k) -> Transistor Q1 (PN2222A) Base
-Transistor Q1 (PN2222A) Collector -> Relay K1 (SANYOU SRD) Coil
-Diode D3 (1N4007) -> In parallel with Relay K1 Coil
-Relay K1 (SANYOU SRD) -> Dry-contact Output Terminals
-ESP32 (GPIO18) -> Resistor R1 (220) -> LED D1
-ESP32 (GPIO19) -> Resistor R2 (220) -> LED D2

2. Intelligent Thermometer/Thermostat Module:
-5V Power Supply -> Potentiometer RV1 (10k) -> LCD DS1 (WC1602A) V0 Pin 
-ESP32 (GPIO) -> Transistor Q1 (PN2222A) Base
-Transistor Q1 (PN2222A) Collector -> LCD DS1 (WC1602A) K(-) Pin
-Sensor DHT11 (DATA Pin) -> ESP32 (GPIO16)
-3V3 Power Supply -> Photoresistor R_Photo -> ESP32 (GPIO32/ADC)
-ESP32 (GPIO32/ADC) -> Resistor R2 (10k) -> GND
-Push Button SW1 -> ESP32 (GPIO)
-Push Button SW2 -> ESP32 (GPIO)
