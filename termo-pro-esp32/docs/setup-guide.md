Hardware Assembly:
-The prototype was built using two T830 breadboards. 
-To set up this thermostat, follow the electrical schematic provided in the .pdf or KiCad files.
-I recommend using large breadboards for an easier setup or designing a PCB based on the schematics to solder the components permanently.
-Relay connection to heating boiler is done via the NO-COM to NO-COM or TA/TA.

GPIO Pins used for thermostat esp32:
-DHT11 Sensor,23 
-LCD Display," 4, 5, 16, 17, 18, 19 "
-LCD Brightness (PWM), 12
-Buttons,"32, 33"
-Photoresistor (LDR), 34

GPIO Pins for starting-module esp32:
-Relay,17 - > Relay is done fully as in the scheme, but the 2 leds are connected into the relay board, so only pin 17 is used , if done manually you will require two more pins for the LEDs.

*Obs:
***-All this is done on esp32-wroom-32 devkit, diffrent versions of esp32 have other specs for pins, be careful how you wire the connections!***
***-Max pin current is I = 12mA.***


////////////////////////////////////////////////////////////////////////////

*Obs(if not using pre-made relay modules):
-For the startingModule, the 2 LED on the electrical scheme are not added in code in file startingModule.ino, you will need to manually add the pin numbers and other required code, comments are provided in code for easy setup ;).

-Pre-made relay uses an PNP/NPN transistor, that means its high-side for PNP and low-side for NPN. I used an NPN transistor for switching because the pin only supplies 3.3V not 5V that an PNP needs, some relays also support 3.3V to open, but not reliable.

-Pin current needs to be at least 2-3mA for the transistor base to open and turn the relay coil on.
-Used 1kOhm resistor, but 470/330 works too!
-Rectifier diode 1N4007 can be replaced with 1N4001 / 1N4148.
    
***If using an high-side relay module, do change the 3 lines of code inside startingModule.ino, just reverse them , HIGH at boot, LOW on start and HIGH on stop.***

***Obs: On the thermostat.pdf/sch you will see a component U3, that is an MB-V2 breadboard power module(it has both 5V and 3.3V).***
