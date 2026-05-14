--- Starting Unit ---
    - Relay flyback diode -> diode that can handle minimum 200mA, I have chosen 1N4007 because is cheap and easy to find, and handles a minimum of 1A, and can even to 3A.
    - PN2222A NPN BJT Transistor, its cheap and good for general use as a switch, using a MOSFET is more expensive.
    - Resistance of the transistor that drives the relay is calculated using Ohm law, the relay takes around 80mA, taking a B = 10 as minimum , we need ~7-8 mA in transistor base , an 330 Ohm resistance is used because:
        I = U / R => I = (3.3 - 0.7(TB)) / 330 => I ~ 7.8 mA , so 330 ohm is enough to saturate the transistor for optimal operations.
    - 10k B3950A thermistor, to be read by ADC pin requires another 10k resistor to form a tension divider for reading to be possible.
    - 12.5mm LED button requires an 470 ohm resistor because internal diode uses 1.8V an 220 ohm resistance is used => I = 3.3 - 1.8 / 220 => I = 6.8mA , that is decent in brightness.
    - The 2 LED , green and red, used 220 ohm used for both , because red is 2.0v and green is 1.8V , so they are close in luminosity, also current is under 6mA per pin.
    - Capacitor values C1,C2 and C3:
        *C1 is the main capacitor that is positioned at the begining of the USB-C 5VDC, the value of 220uF is chosen to surpress current spikes for the general circuit(not the ESP32-C3-Supermini, not complete);
        *C3 is the main capacitor positioned close to the ESP32-C3 with a value of 470uF , this and C1 in combination is to surpress the current spikes of the ESP32-C3, as calculated by tge formula:
            C = I * dt / dV => C = 0.335A(spike current) * 0.0005s(500ms) / 0.3V(averge) => C = 558uF, this value is not easy to find so C3 is 470uF and C1 compensates, capacitor in paralel sum there capacitance, so C13 = 690uF, that is enough;
        *C2 the 220nF capacitor si placed very close to the ESP32 5V(VIN) pin , this is used for bypass.
    
--- Termostat Unit ---
    - Buzzer flyback diode, same reason as for the relay.
    - Transistor BJT NPN PN2222A driving the buzzer has a base resistance of 1kOhm, enough to saturate it I = 3.3 - 0.7 / 1kOhm => I ~ 2.6mA , buzzer takes from 15mA to 30mA , so 2.6mA is enough for saturation.
    - Transistor BJT NPN PN2222A driving the display brightness usign PWM, used base resistor 330Ohm , as display maximum current is around 100mA.
    - 10k LDR has anoter 10k resistance for tension divider.
    - RM65 Potentiometer for the display.
    - DHT11 raw module needs an 10k pull-up to 3.3V, so to be safe for GPIO.
    - Capacitors values C1,C2,C3 and C4:
        *C1&C3 ESP32WROOM32-DevKitC/32S has current spikes during WIFI operations to up to 500mA, calculated electrolithic capacitance is 750uF, another value hard to get , so an 680uF and 220uF has been chosen , 560uF at the 5V VIN of the esp32 and 220uF at the USB-C 5VDC;
        *C2 the 220nF capacitor si placed very close to the ESP32 5V(VIN) pin , this is used for bypass.
        *C4 another 220nF capacitor is again for bypass and stability of the DHT11 sensor.
