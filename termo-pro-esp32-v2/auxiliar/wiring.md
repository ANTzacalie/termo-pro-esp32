///!/// This is only for breadboard assembly, not required for PCB as its already done, not including non-GPIO pins! ///!/// 



**** ESP32-C3-SuperMini Starting Unit GPIO PINS USE ****

- GPIO0 -> external temperature sensor before 10k resistor;
- GPIO1 -> unconected;
- GPIO2 -> unconected;
- GPIO3 -> resistor -> 12mm LED button(+);
- GPIO5 -> resistor -> green_led;
- GPIO6 -> resistor -> red_led;
- GPIO7 -> resistor -> PN2222A base;
- GPIO8,9,10,20,21 unconected
- 5V(VIN) to 5V ~ 6V;
- 3.3V -> external temperature sensor(+);
- GND -> GND;



**** ESP32-NodeMCU32S Thermostat Unit GPIO PINS USE ****

- GPIO21 -> DHT11 DATA;
- GPIO22 -> resistor -> PN2222A Base(Display);
- GPIO23,25,26,27 -> buttons;
- GPIO13 -> resistor -> PN2222A Base(Buzzer);
- GPIO19 -> E;
- GPIO18 -> RS;
- GPIO17 -> D7;
- GPIO4 -> D4;
- GPIO14 -> D5;
- GPIO16 -> D6;
- GPIO34 -> between 10k LDR and 10k resistor;
- 5V -> 5V ~ 6V;
- 3.3V -> 10k LDR;
