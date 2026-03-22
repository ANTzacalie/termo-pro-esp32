# Setup Guide

## 1. Overview

This document describes the hardware setup and GPIO configuration for:

- Starting Module (Ignition Control)
- Intelligent Thermostat Module

All testing was performed using the ESP32-WROOM-32 DevKit.

---

# Hardware Assembly

The prototype was assembled using two T830 breadboards.

Follow the electrical schematic provided in the PDF or KiCad project files for exact wiring.
On the  electrical schematics there is an symbol marked U3, it represents the MB-V2 Breadboard power supply, you can use any other power supply, but make sure it can output at least 600mA !

For easier assembly:
- Use large breadboards for better routing.
- Keep power and signal lines separated where possible.
- Keep relay wiring isolated from low-voltage logic wiring.

For a permanent installation, design and manufacture a PCB based on the schematic and solder all components.

---

# Boiler Connection

The relay connects to the heating boiler using:

- NO–COM terminals  
  or  
- TA/TA terminals (depending on boiler labeling)

Ensure the boiler control circuit is voltage-free (dry contact) before connecting.

---

# Thermostat Module – GPIO Configuration

The thermostat ESP32 uses the following pins:

DHT11 (Temperature & Humidity Sensor): GPIO 23  

LCD Display (Parallel Interface):
GPIO 4 
GPIO 5 
GPIO 16  
GPIO 17  
GPIO 18  
GPIO 19  

LCD Backlight Brightness (PWM): GPIO 12  

Push Buttons:
GPIO 32  
GPIO 33  

Photoresistor (LDR – Analog Input): GPIO 34  

---

# Starting Module – GPIO Configuration

Relay Control: GPIO 17  

If using a pre-made relay module:
- Only GPIO 17 is required.
- Status LEDs are already integrated on the module.

If building the relay driver manually:
- Two additional GPIO pins are required for LED indicators.
- LEDs must also be defined in the firmware.

---

# Electrical Considerations

Maximum recommended GPIO current:  
12 mA per pin  

Ensure:
- Proper base resistor sizing for transistor switching.
- Common ground between ESP32 and relay driver circuit.
- Flyback diode across relay coil.

---

# Manual Relay Driver Notes (Without Pre-Made Module)

The startingModule.ino file does not include control logic for the two schematic LEDs.  
If using the discrete version, manually define the LED pins and add control logic. Comments are already included in the code for guidance.

Relay switching configuration:

- NPN transistor used (low-side switching)
- ESP32 logic level: 3.3 V

Reason:
Most PNP high-side relay modules require 5 V logic for reliable operation.  
Using an NPN transistor ensures proper switching with 3.3 V control signals.

Recommended base current:  
2–3 mA minimum to ensure transistor saturation.

Base resistor values tested:
- 1 kΩ (used)
- 470 Ω (acceptable)
- 330 Ω (acceptable)

Flyback diode:
- 1N4007 (used)
- 1N4001 (compatible)
- 1N4148 (acceptable for small relay coils)

---

# Important Warning

Different ESP32 variants may have different GPIO capabilities and restrictions.

Before wiring:
- Verify which pins support PWM.
- Verify which pins support ADC.
- Avoid strapping pins unless properly understood.

# *** WARNING ***
- Incorrect wiring may permanently damage the microcontroller.
- It is recomanded to ansamble step by step and test each one.
