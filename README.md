# ESP32 Smart Thermostat (V1)

<p align="center">
<img src="https://img.shields.io/badge/code-C%2B%2B-00599C?logo=c%2B%2B&logoColor=white">
<img src="https://img.shields.io/badge/board-ESP32-E7352C?logo=espressif&logoColor=white">
<img src="https://img.shields.io/badge/communication-ESP--NOW-yellow">
<img src="https://img.shields.io/badge/PCB-KiCad-314CB0?logo=kicad&logoColor=white">
<img src="https://img.shields.io/badge/IDE-Arduino-00979D?logo=arduino&logoColor=white">
<img src="https://img.shields.io/badge/domain-IoT-ff6f00">
<img src="https://img.shields.io/badge/hardware-Sensors%20%26%20Electronics-6a1b9a">
</p>


A fully functional, smart thermostat built on the ESP32 platform using the ESP_NOW protocol for wireless communication. This unit features 6 programmable modes, a persistent flash-memory system, and an automated brightness control system.

## 💎 Features

- **Protocol:** ESP-NOW for fast, low-power communication with heating units.  
- **Storage:** All programs and settings are stored in Flash memory (NVS) to prevent data loss during power outages.  
- **Display:** 16x2 LCD providing real-time data and a menu-driven configuration system.  
- **Auto-Brightness:** Integrated photoresistor (LDR) logic that adjusts LCD backlight based on ambient light.  

- **Programs:**  
  - **Program B (Basic):** Pre-configured (Start: 21°C / Stop: 23°C).  
  - **Programs 1–5:** User-programmable (Empty by default).  
  - **Program 6/A:** Dedicated Comfort/Auto mode.  

---

## 🖥 User Interface Guide

### Main Interface [A]

This is the default screen. It displays the following layout:  

`T: vv.v H: vv.v PN XX:XX DAY[N] D/N O/F`

| Indicator | Description |
|---------|-------------|
| T | Current Temperature (Celsius) |
| H | Relative Humidity (%RH) |
| P | Program Mode |
| N | Program ID Number |
| XX:XX | Current Time (Hours:Minutes) |
| DAY [N] | Current Day of the week (1–7) |
| D/N | Day / Night Status |
| O/F | Heating Status (On / Off) |

---

### Navigation & Settings [B]

To access the configuration menu, press and hold **Button 1** for **2 seconds**.  

> **Note:** Entering Interface [B] pauses the thermostat's main control logic until you exit.

#### Controls in Menus

- **Button 2:** Moves the cursor (indicated by an arrow `->`).  
- **Button 1:** Selects the highlighted option or increments values.  

#### Menu Options

- **SPG (Select Program):** Choose which of the 6 programs to activate. Selecting a program returns you to Menu [B].  

- **EPG (Edit Program):**  
  - Select the program you wish to modify.  
  - Line 1 shows the Program ID; Line 2 shows Start Temperature and Stop Temperature.  
  - Button 2 selects the value; Button 1 increments the temperature.  
  - **Save Logic:** After 10 seconds of inactivity, the data is saved to memory, set as the active program, and the system returns to Menu [B].  

- **ETM (Edit Time):**  
  - Modify Hour, Minute, and Day.  
  - Button 2 moves the cursor; Button 1 increments the value.  
  - **Save Logic:** Settings save automatically after 10 seconds of inactivity.  

- **EXIT:** Returns to the Main Interface [A].  

---

## 🛠 Special Commands & Manual Overrides

These actions are performed directly from the Main Interface [A] using timed button presses:

| Action | Button Combo | Duration | Result |
|------|-------------|----------|--------|
| System Restart | Button 1 + Button 2 | 15 Seconds | Device reboots |
| Manual Mode | Button 2 | 5 Seconds | Disables auto-logic and active program |
| Manual OFF | Button 2 | 5 Seconds | While in Manual Mode: Turns off the heating module |
| Manual ON | Button 1 + Button 2 | 10 Seconds | While in Manual Mode: Turns on the heating module |
| Exit Manual | Button 2 | 10 Seconds | Cancels manual control and restores Auto Program logic |

---

## 🚀 Future Roadmap - V2 ;)
- An PCB and 3D printed case, also improved functionalities and better starting module microcontroller.
- 📱 **Mobile Application:** A dedicated app for Android to control the thermostat remotely via Bluetooth (BLE). This will allow for easy program editing and manual overrides without using the physical buttons.  

