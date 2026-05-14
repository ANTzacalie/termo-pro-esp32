#pragma once

// Buttons
const int button_meniu = 32;
const int button_down = 33;
const int button_up = ?;
const int button_save = ?;

// Photoresistor pin
const int ldrPin = 34; // input-only, analog pin

// PWM setup for display brightness
const int pwmPinDisplay = 12;
const int freqDisplay = 5000;
const int pwmChannelDisplay = 0;
const int resolutionDisplay = 8; // 8-bit

// Retry & reboot variables
extern int counter;
extern int failed_receive;
extern bool last_command;

// Receiver MAC (firing module)
extern uint8_t receiverMAC[];

// Day/Night
extern int day_start;
extern int night_start;

// Time
extern int hour;
extern int day;

// Program control
extern int program_choice;

// Manual control
extern bool manual_control;
const int force_stop = 15000;   // 15s

// Programs
const int program_1 = 1;
const int program_2 = 2;
const int program_3 = 3;
const int program_4 = 4;
const int program_auto = 6;
const int program_basic = 5;

// flag to start tracking time , only used for auto mode
extern int record_flag;

// -------------------- AUTO MODE --------------------
class auto_mode {

public:

    float START_TEMP = 0;
    float END_TEMP = 0;

    const int reset_adjustment = 7;
    int day_increment = 0;

    // ///////////////////////////
    float adjust_start_temp_day = 0;
    float adjust_start_temp_night = 0;

    // time in minutes;
    int time_night = 0;
    int time_day = 0;

    bool calc_co_1 = true;
    bool calc_co_2 = true;

    int ntc_room_day = 0;
    int delta_dynamic_day;
    int co_1 = 0;
    int check_1 = 0;

    long int ntc_room_night = 0;
    int delta_dynamic_night;
    int co_2 = 0;
    int check_2 = 0;

    const int FAST_COOL = 120;  // min
    const int SLOW_COOL = 480;  // min
    const int MAX_PREHEAT = 60; // max early start

    float comfort_factor = 1.0;

    // ///////////////////////////

    void adjust_ntc_day() {

        if (!calc_co_1) return;

        co_1 += time_day;
        check_1++;

        if (check_1 == 2) {

            check_1 = 0;
            co_1 /= 2;
            ntc_room_day = co_1;
            calc_co_1 = false;
            compute_day_adjust();

        }

    }

    void adjust_ntc_night() {

        if (!calc_co_2) return;

        co_2 += time_night;
        check_2++;

        if (check_2 == 2) {

            check_2 = 0;
            co_2 /= 2;
            ntc_room_night = co_2;
            calc_co_2 = false;
            compute_night_adjust();

        }

    }

    void set_time_ntc(int minutes_passed) {

        if(hour > day_start && hour < night_start) {

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
        if (t > SLOW_COOL) t = SLOW_COOL;

        float factor = float(SLOW_COOL - t) / float(SLOW_COOL - FAST_COOL);

        delta_dynamic_day = factor * MAX_PREHEAT * comfort_factor;
        adjust_start_temp_day = START_TEMP + temp_offset(delta_dynamic_day);

    }

    void compute_night_adjust() {

        int t = ntc_room_night;
        if (t < FAST_COOL) t = FAST_COOL;
        if (t > SLOW_COOL) t = SLOW_COOL;

        float factor = float(SLOW_COOL - t) / float(SLOW_COOL - FAST_COOL);

        delta_dynamic_night = factor * MAX_PREHEAT * comfort_factor;
        adjust_start_temp_night = START_TEMP + temp_offset(delta_dynamic_night);

    }

    float temp_offset(int minutes) {

        return (minutes / 60.0f) * 0.5f;

    }

};

extern auto_mode confort_mode;

// -------------------- Seasonal Programs --------------------
class prog_1 { 
    
    public:

    float START_TEMP = 0; 
    float STOP_TEMP = 0;
    
};

class prog_2 { 
    
    public:

    float START_TEMP = 0; 
    float STOP_TEMP = 0;

};

class prog_3 { 
    
    public:

    float START_TEMP = 0; 
    float STOP_TEMP = 0;
    
};

class prog_4 { 
    
    public:

    float START_TEMP = 0;
    float STOP_TEMP = 0;

};

//////////////////////////////////////

// a simple mode to control the fire module, also default
class basic {

public:
    float START_TEMP = 20;
    float STOP_TEMP = 23;

};

