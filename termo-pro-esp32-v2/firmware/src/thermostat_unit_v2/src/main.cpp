#include "thermostat.h"

//  GLOBAL DEFINITIONS  (extern declarations live in thermostat_var.h)

// retry & safety
int  counter        = 0;
int  failed_receive = 0;
bool last_command   = false;

// Receiver MAC — Starting Unit (replace with real MAC before flashing!)
uint8_t receiverMAC[6] = { 0xD4, 0x8A, 0xFC, 0xA2, 0xA0, 0xD8 };

// time / day-night
int day_start   = 5;    // 05:00
int night_start = 21;   // 21:00
int hour        = 0;
int minute      = 0;
int day         = 0;    // weekday 1-7 (Mon=1, Sun=7)

// program state
int  program_choice  = 0;
bool manual_control  = false;
int  record_flag     = 0;

// auto mode instance  (extern in thermostat_var.h)
auto_mode confort_mode;

// ESP-NOW class static instance pointer (defined in thermostat.h)
local* local::instance = nullptr;


//  PROGRAM INSTANCES
auto_mode program_auto;
prog_1    program1;
prog_2    program2;
prog_3    program3;
prog_4    program4;
basic     program0;

//  LCD BACKLIGHT BRIGHTNESS THRESHOLDS  (12-bit ADC values)
enum brightness_control : uint16_t {

    LOW_BC    = 1200,
    LOW_H_BC  = 1800,
    MEDIUM_BC = 2400,
    INTER_NC  = 2900,
    HIGH_BC   = 3400,
    ULTRA_BC  = 3800,
    ULTRA_H_BC = 4000

};


// Set the internal RTC from explicit hour/minute/weekday values.
// Uses a fixed dummy date (Mon 1 Jan 2024) shifted by weekday offset.
static void set_time(int h, int m, int weekday) {

    struct tm t = {};

    t.tm_year  = 2024 - 1900;
    t.tm_mon   = 0;
    t.tm_mday  = 1 + (weekday - 1); // align calendar weekday
    t.tm_hour  = h;
    t.tm_min   = m;
    t.tm_sec   = 0;
    t.tm_isdst = -1;

    time_t epoch = mktime(&t);

    struct timeval tv;
    tv.tv_sec  = epoch;
    tv.tv_usec = 0;

    settimeofday(&tv, nullptr);

}

// Read current time back from the internal RTC.
static void read_time(int &h, int &m, int &weekday) {

    time_t now = time(nullptr);
    struct tm ti;
    localtime_r(&now, &ti);

    h       = ti.tm_hour;
    m       = ti.tm_min;
    weekday = (ti.tm_wday == 0) ? 7 : ti.tm_wday; // Sun = 0 -> 7

}


//  START / STOP LOGIC
// Returns 1 and sets record_flag=1 when heating should start.
int check_for_start(float current_temp, float start_temp) {

    if (current_temp <= start_temp) {
        record_flag = 1;
        return 1;
    }
    return 0;

}

// Returns 1 and sets record_flag=2 when heating should stop.
int check_for_stop(float current_temp, float stop_temp) {

    if (current_temp >= stop_temp) {
        record_flag = 2;
        return 1;
    }
    return 0;

}


//  CUSTOM LCD CHARACTERS(ARROW UP)
static uint8_t arrowUp[8] = {

    0b00100,
    0b01110,
    0b11111,
    0b00100,
    0b00100,
    0b00100,
    0b00100,
    0b00100

};

void splash_1() {

    // splash screen 
    lcd_row_sel(0);
    lcd.print("TermoPro v2.0");
    lcd_row_sel(1);
    lcd.print("by ANTzacalie");
    delay_s(3000);

    lcd.clear();
    lcd_row_sel(0);
    lcd.print("For bugs go to");
    lcd_row_sel(1);
    lcd.print("---- GitHub ----");
    delay_s(1000);

}

void splash_2() {

    lcd_row_sel(0);
    lcd.print(" TermoPro v2.0 ");

    
    for(int i = 0; i < 16; i+=1) {

        lcd.setCursor(i, 1);
        lcd.print(".");
        delay_s(200);

    }

}

// MAIN
extern "C" void app_main() {

    // static ESP-NOW handler instance 
    static local command;
    local::instance = &command;

    // peripheral init 
    gpio_buttons_init();
    adc_ldr_init();
    ledc_backlight_init();

    // Wi-Fi + ESP-NOW 
    wifi_espnow_init();

    // DHT11 
    dht.begin();

    // LCD 
    lcd.begin(16, 2);
    lcd.clear();
    // custom arrow_up loaded into display
    lcd.createChar(0, arrowUp);

    // initial brightness 
    sensor.setBrightness(150);

    // NVS / preferences 
    storage.begin();

    delay_s(500); // stabilization

    splash_1();
    lcd.clear();
    splash_2();

    delay_s(1000);
    lcd.clear();

    while (true) {

        /*
                TEST
        */
        delay_s(5000);
        sensor.readTemperatureHumidity();
        lcd.print(sensor.getTemperature());

    }

}
