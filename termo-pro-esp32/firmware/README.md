Software Setup:
-I used Arduino IDE v2, but the code is compatible with other environments as long as you have the following libraries installed: 

-Adafruit Unified Sensor (v1.1.15) by Adafruit. 
-DHT Sensor Library (v1.4.6) by Adafruit.
-LiquidCrystal (Standard library for 4-bit/8-bit mode).

***Very important, get the mac address from both of your ESP32s!!!***
How to get it?
-Using this command on an empty sketch: 

setup() {

  Serial.begin(115200);
  WiFi.mode(WIFI_STA);
  delay(100); 
  
  Serial.print("ESP32 Board MAC Address:  ");
  Serial.println(WiFi.macAddress());

}

-It will get you the mac address in the serial monitor. Copy both and place them at the specified locations below , you will find an array named receiverMAC:
-termostat esp32 mac -> startingModule.ino;
-startingModule esp32 mac -> termostat.ino;

-termostat.ino dependencies(both of below need to be in the same folder): 
    - termostat.h;
    - termostat_variables.h;
-starting-module.ino has no external dependencies;
