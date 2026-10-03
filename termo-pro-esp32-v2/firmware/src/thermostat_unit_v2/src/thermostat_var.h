#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"

// -------------------- Button GPIO pins --------------------
static constexpr gpio_num_t BUTTON_UP_PIN   = GPIO_NUM_26;
static constexpr gpio_num_t BUTTON_DOWN_PIN = GPIO_NUM_27;
static constexpr gpio_num_t BUTTON_MENU_PIN = GPIO_NUM_25;
static constexpr gpio_num_t BUTTON_SAVE_PIN = GPIO_NUM_23;

// -------------------- Photoresistor (ADC) --------------------
// ADC1 channel 6 = GPIO34 on WROOM-32 (input-only pin, no pull)
static constexpr gpio_num_t LDR_PIN = GPIO_NUM_34;

// -------------------- LCD backlight PWM (LEDC) --------------------
static constexpr gpio_num_t PWM_PIN_DISPLAY  = GPIO_NUM_22;
static constexpr uint32_t   LEDC_FREQ_HZ     = 5000;
#define LEDC_CH_DISPLAY     LEDC_CHANNEL_0
#define LEDC_SPEED_DISPLAY  LEDC_LOW_SPEED_MODE
#define LEDC_TIMER_DISPLAY  LEDC_TIMER_0
#define LEDC_RESOLUTION     LEDC_TIMER_8_BIT   // 0-255

// -------------------- Retry & reboot counters --------------------
extern int  counter;
extern int  failed_receive;
extern bool last_command;

// -------------------- ESP-NOW peer MAC (Starting Unit) --------------------
extern uint8_t receiverMAC[6];

// -------------------- Day / Night boundary hours --------------------
extern int day_start;    // e.g. 5  (05:00)
extern int night_start;  // e.g. 21 (21:00)

// -------------------- Soft-RTC shadow --------------------
extern int hour;
extern int minute;
extern int day;    // weekday 1-7 (Mon=1, Sun=7)

// -------------------- Program selector --------------------
extern int  program_choice;
extern bool manual_control;

// -------------------- Auto-mode timing flag --------------------
// 0 = idle, 1 = heating started (record cool-down start), 2 = stopped
extern int record_flag;


// ============================================================
//  AUTO MODE
// ============================================================
class auto_mode {

public:

    float START_TEMP = 0;
    float END_TEMP   = 0;

    const int reset_adjustment = 7;
    int       day_increment    = 0;

    // adjusted preheat start temperatures
    float adjust_start_temp_day   = 0;
    float adjust_start_temp_night = 0;

    // measured room cool-down times (minutes)
    int time_night = 0;
    int time_day   = 0;

    // calculation flags
    bool calc_co_1 = true;
    bool calc_co_2 = true;

    // day averager
    int ntc_room_day     = 0;
    int delta_dynamic_day;
    int co_1   = 0;
    int check_1 = 0;

    // night averager
    long int ntc_room_night = 0;
    int delta_dynamic_night;
    int co_2   = 0;
    int check_2 = 0;

    const int FAST_COOL  = 120;   // minutes
    const int SLOW_COOL  = 480;   // minutes
    const int MAX_PREHEAT = 60;   // max early-start minutes

    float comfort_factor = 1.0f;

    // ----------------------------------------------------------
    void adjust_ntc_day() {

        if (!calc_co_1) return;

        co_1 += time_day;
        check_1++;

        if (check_1 == 2) {
            check_1 = 0;
            co_1   /= 2;
            ntc_room_day = co_1;
            calc_co_1    = false;
            compute_day_adjust();
        }

    }

    void adjust_ntc_night() {

        if (!calc_co_2) return;

        co_2 += time_night;
        check_2++;

        if (check_2 == 2) {
            check_2 = 0;
            co_2   /= 2;
            ntc_room_night = co_2;
            calc_co_2      = false;
            compute_night_adjust();
        }

    }

    void set_time_ntc(int minutes_passed) {

        if (hour > day_start && hour < night_start) {
            time_day = minutes_passed;
            adjust_ntc_day();
        } else {
            time_night = minutes_passed;
            adjust_ntc_night();
        }

    }

private:

    void compute_day_adjust() {

        int t = ntc_room_day;
        if (t < FAST_COOL) t = FAST_COOL;
        if (t > SLOW_COOL)  t = SLOW_COOL;

        float factor       = float(SLOW_COOL - t) / float(SLOW_COOL - FAST_COOL);
        delta_dynamic_day  = int(factor * MAX_PREHEAT * comfort_factor);
        adjust_start_temp_day = START_TEMP + temp_offset(delta_dynamic_day);

    }

    void compute_night_adjust() {

        int t = ntc_room_night;
        if (t < FAST_COOL) t = FAST_COOL;
        if (t > SLOW_COOL)  t = SLOW_COOL;

        float factor         = float(SLOW_COOL - t) / float(SLOW_COOL - FAST_COOL);
        delta_dynamic_night  = int(factor * MAX_PREHEAT * comfort_factor);
        adjust_start_temp_night = START_TEMP + temp_offset(delta_dynamic_night);

    }

    float temp_offset(int minutes) {

        return (minutes / 60.0f) * 0.5f;

    }

};

extern auto_mode confort_mode;


//  SEASONAL PROGRAMS  (START_TEMP / STOP_TEMP set at runtime)
/*

    TODO: ADV. PROGRAM SET TBD

*/
class prog_1 {
public:
    float START_TEMP = 0;
    float STOP_TEMP  = 0;
};

class prog_2 {
public:
    float START_TEMP = 0;
    float STOP_TEMP  = 0;
};

class prog_3 {
public:
    float START_TEMP = 0;
    float STOP_TEMP  = 0;
};

class prog_4 {
public:
    float START_TEMP = 0;
    float STOP_TEMP  = 0;
};

// -------------------- Basic / default program --------------------
class basic {
public:
    float START_TEMP = 20;
    float STOP_TEMP  = 23;
};
