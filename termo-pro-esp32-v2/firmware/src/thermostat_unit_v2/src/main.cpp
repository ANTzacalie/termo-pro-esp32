#include "thermostat.h"

//  GLOBAL DEFINITIONS  (extern declarations live in thermostat_var.h)

// retry & safety
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
float ext_temp = NAN;

// The clock must be configured before an hourly program can allow heating.
bool time_initialized = false;
int counter = 0;
int failed_receive = 0;

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

    LOW_BC     = 1200,
    LOW_H_BC   = 1800,
    MEDIUM_BC  = 2400,
    INTER_NC   = 2900,
    HIGH_BC    = 3400,
    ULTRA_BC   = 3800,
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


//  NVS STORAGE  (Preferences wrapper — persisted settings and hourly programs)
class data {

private:
    nvs_handle_t nvs_handle = 0;

    void get_float(const char* key, float &destination) {

        float value = 0.0f;
        size_t bytes = sizeof(value);

        if (nvs_get_blob(nvs_handle, key, &value, &bytes) == ESP_OK &&
            bytes == sizeof(value) && isfinite(value) &&
            value >= 5.0f && value <= 35.0f) {
            destination = value;
        }

    }

    void get_integer(const char* key, int &destination) {

        int32_t value = 0;

        if (nvs_get_i32(nvs_handle, key, &value) == ESP_OK) {
            destination = value;
        }

    }

    bool get_program(int id, float &start, float &stop) {

        switch (id) {

            case 0:
                start = program0.s_temp;
                stop  = program0.p_temp;
                return true;
            case 1:
                start = program1.get_stemp();
                stop  = program1.get_ptemp();
                return true;
            case 2:
                start = program2.get_stemp();
                stop  = program2.get_ptemp();
                return true;
            case 3:
                start = program3.get_stemp();
                stop  = program3.get_ptemp();
                return true;
            case 4:
                start = program4.get_stemp();
                stop  = program4.get_ptemp();
                return true;
            case 5:
                start = confort_mode.get_stemp();
                stop  = confort_mode.get_ptemp();
                return true;
            default:
                return false;

        }

    }

    void put_program(int id, float start, float stop) {

        switch (id) {

            case 1:
                program1.set_stemp(start);
                program1.set_ptemp(stop);
                break;
            case 2:
                program2.set_stemp(start);
                program2.set_ptemp(stop);
                break;
            case 3:
                program3.set_stemp(start);
                program3.set_ptemp(stop);
                break;
            case 4:
                program4.set_stemp(start);
                program4.set_ptemp(stop);
                break;
            case 5:
                confort_mode.set_stemp(start);
                confort_mode.set_ptemp(stop);
                break;

        }

    }

public:

    // Call once at boot — opens NVS namespace and loads saved values.
    void begin() {

        // Safe defaults for the user-editable programs when NVS is blank.
        for (int i = 1; i <= 5; i++) {
            put_program(i, 20.0f, 23.0f);
        }

        esp_err_t err = nvs_open("programs", NVS_READWRITE, &nvs_handle);
        if (err != ESP_OK) {
            ESP_LOGE(TAG_TH, "NVS open failed: %s", esp_err_to_name(err));
            return;
        }

        load();
        nvs_close(nvs_handle);

    }

    // Call whenever the user changes a program value.
    void save() {

        esp_err_t err = nvs_open("programs", NVS_READWRITE, &nvs_handle);
        if (err != ESP_OK) {
            ESP_LOGE(TAG_TH, "NVS open (save) failed: %s", esp_err_to_name(err));
            return;
        }

        nvs_set_i8(nvs_handle, "l_cmd", last_command ? 1 : 0);
        nvs_set_blob(nvs_handle, "rec_mac", receiverMAC, sizeof(receiverMAC));
        nvs_set_i32(nvs_handle, "day_s", day_start);
        nvs_set_i32(nvs_handle, "night_s", night_start);
        nvs_set_i32(nvs_handle, "hour", hour);
        nvs_set_i32(nvs_handle, "minute", minute);
        nvs_set_i32(nvs_handle, "day", day);
        nvs_set_i8(nvs_handle, "time_set", time_initialized ? 1 : 0);
        nvs_set_i32(nvs_handle, "prg_ch", program_choice);
        nvs_set_i32(nvs_handle, "ntc_r_d", confort_mode.ntc_room_day);
        nvs_set_i32(nvs_handle, "ntc_r_n", confort_mode.ntc_room_night);

        // Manual control and last heating command are NOT restored at boot.
        // Restart always begins with the boiler command OFF.
        save_programs();
        err = nvs_commit(nvs_handle);

        if (err != ESP_OK) {
            ESP_LOGE(TAG_TH, "NVS commit failed: %s", esp_err_to_name(err));
        }

        nvs_close(nvs_handle);

    }

    void save_programs() {

        for (int i = 1; i <= 5; i++) {

            float s_temp = 0.0f;
            float p_temp = 0.0f;

            if (!get_program(i, s_temp, p_temp)) {
                continue;
            }

            char start_key[12];
            char stop_key[12];
            snprintf(start_key, sizeof(start_key), "s_p%d", i);
            snprintf(stop_key, sizeof(stop_key), "p_p%d", i);

            nvs_set_blob(nvs_handle, start_key, &s_temp, sizeof(s_temp));
            nvs_set_blob(nvs_handle, stop_key,  &p_temp, sizeof(p_temp));

            if (i <= 4) {
                uint32_t hours_mask = 0x00FFFFFFUL;

                switch (i) {
                    case 1:
                        hours_mask = program1.allowed_hours;
                        break;
                    case 2:
                        hours_mask = program2.allowed_hours;
                        break;
                    case 3:
                        hours_mask = program3.allowed_hours;
                        break;
                    case 4:
                        hours_mask = program4.allowed_hours;
                        break;
                }

                char hour_key[12];
                snprintf(hour_key, sizeof(hour_key), "hours_p%d", i);
                nvs_set_u32(nvs_handle, hour_key, hours_mask & 0x00FFFFFFUL);
            }

        }

    }

private:

    void load() {

        // Restore the saved settings and all five editable programs.
        get_integer("day_s", day_start);
        get_integer("night_s", night_start);
        get_integer("hour", hour);
        get_integer("minute", minute);
        get_integer("day", day);
        get_integer("prg_ch", program_choice);

        day_start = (day_start >= 0 && day_start <= 23) ? day_start : 5;
        night_start = (night_start >= 0 && night_start <= 23) ? night_start : 21;
        hour = (hour >= 0 && hour <= 23) ? hour : 0;
        minute = (minute >= 0 && minute <= 59) ? minute : 0;
        day = (day >= 1 && day <= 7) ? day : 1;
        program_choice = (program_choice >= 0 && program_choice <= 5) ? program_choice : 0;

        int8_t saved_clock_flag = 0;
        if (nvs_get_i8(nvs_handle, "time_set", &saved_clock_flag) == ESP_OK) {
            time_initialized = (saved_clock_flag != 0);
        }

        // The existing automatic cool-down values use these original NVS keys.
        int cool_time = 0;
        get_integer("ntc_r_d", cool_time);
        confort_mode.ntc_room_day = cool_time >= 0 ? cool_time : 0;
        cool_time = 0;
        get_integer("ntc_r_n", cool_time);
        confort_mode.ntc_room_night = cool_time >= 0 ? cool_time : 0;

        // A MAC saved in NVS can override the compile-time MAC.
        uint8_t stored_mac[6] = {};
        size_t mac_size = sizeof(stored_mac);
        if (nvs_get_blob(nvs_handle, "rec_mac", stored_mac, &mac_size) == ESP_OK &&
            mac_size == sizeof(stored_mac)) {
            bool nonzero_mac = false;
            for (uint8_t octet : stored_mac) {
                nonzero_mac |= (octet != 0);
            }
            if (nonzero_mac) {
                memcpy(receiverMAC, stored_mac, sizeof(receiverMAC));
            }
        }

        for (int i = 1; i <= 5; i++) {

            float s_temp = 20.0f;
            float p_temp = 23.0f;
            char start_key[12];
            char stop_key[12];

            snprintf(start_key, sizeof(start_key), "s_p%d", i);
            snprintf(stop_key, sizeof(stop_key), "p_p%d", i);
            get_float(start_key, s_temp);
            get_float(stop_key, p_temp);

            if (p_temp < s_temp + 0.5f) {
                p_temp = s_temp + 0.5f;
            }

            if (p_temp > 35.0f) {
                s_temp = 20.0f;
                p_temp = 23.0f;
            }

            put_program(i, s_temp, p_temp);

            if (i <= 4) {
                char hour_key[12];
                uint32_t hours_mask = 0x00FFFFFFUL;

                snprintf(hour_key, sizeof(hour_key), "hours_p%d", i);
                (void)nvs_get_u32(nvs_handle, hour_key, &hours_mask);
                hours_mask &= 0x00FFFFFFUL;

                switch (i) {
                    case 1:
                        program1.allowed_hours = hours_mask;
                        break;
                    case 2:
                        program2.allowed_hours = hours_mask;
                        break;
                    case 3:
                        program3.allowed_hours = hours_mask;
                        break;
                    case 4:
                        program4.allowed_hours = hours_mask;
                        break;
                }
            }

        }

    }

} storage;

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

// Initial splash screen
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
    delay_s(1800);

}

// Secondary splash screen
void splash_2() {

    lcd_row_sel(0);
    lcd.print(" TermoPro v2.0 ");
    
    for(int i = 0; i < 16; i+=1) {

        lcd.setCursor(i, 1);
        lcd.print(".");
        delay_s(200);

    }

}

// ============================================================
//  16x2 LCD INTERFACE / FOUR BUTTON NAVIGATION
// ============================================================
// UP / DOWN: navigate or change a value
// MENU short: select / next field; MENU long: exit / cancel
// SAVE: save / return; selected edits autosave after 10 seconds

enum screen_mode : uint8_t {

    SCREEN_MAIN,
    SCREEN_MENU,
    SCREEN_PROGRAM_SELECT,
    SCREEN_PROGRAM_PICK,
    SCREEN_PROGRAM_EDIT,
    SCREEN_HOUR_EDIT,
    SCREEN_TIME_EDIT,
    SCREEN_DAYNIGHT_EDIT

};

struct button_input {

    gpio_num_t pin;
    bool pressed = false;
    bool long_sent = false;
    uint32_t changed_ms = 0;
    uint32_t down_ms = 0;
    uint32_t repeat_ms = 0;

    // 0 = idle, 1 = short press, 2 = long press, 3 = held repeat
    uint8_t update(uint32_t now) {

        bool down = gpio_get_level(pin) == 0;

        if (down != pressed && (uint32_t)(now - changed_ms) >= 40) {
            pressed = down;
            changed_ms = now;

            if (down) {
                down_ms = now;
                repeat_ms = now;
                long_sent = false;
            } else if (!long_sent) {
                return 1;
            }
        }

        if (pressed && !long_sent &&
            (uint32_t)(now - down_ms) >= 1400) {
            long_sent = true;
            return 2;
        }

        if (pressed && (pin == BUTTON_UP_PIN || pin == BUTTON_DOWN_PIN) &&
            (uint32_t)(now - down_ms) >= 600 &&
            (uint32_t)(now - repeat_ms) >= 150) {
            repeat_ms = now;
            return 3;
        }

        return 0;

    }

};

static button_input key_up   = { BUTTON_UP_PIN };
static button_input key_down = { BUTTON_DOWN_PIN };
static button_input key_menu = { BUTTON_MENU_PIN };
static button_input key_save = { BUTTON_SAVE_PIN };

static screen_mode screen = SCREEN_MAIN;
static int home_page = 0;
static int menu_index = 0;
static int selected_program = 1;
static int edit_field = 0;
static int selected_hour = 0;
static int draft_hour = 0;
static int draft_minute = 0;
static int draft_day = 1;
static int draft_day_start = 5;
static int draft_night_start = 21;
static float draft_start_temp = 20.0f;
static float draft_stop_temp = 23.0f;
static uint32_t draft_hours = 0x00FFFFFFUL;
static bool manual_heat = false;
static bool screen_dirty = true;
static uint32_t last_button_ms = 0;
static uint32_t last_draw_ms = 0;
static uint32_t last_control_ms = 0;
static uint32_t last_sensor_ms = 0;
static uint32_t last_light_ms = 0;
static uint32_t last_command_refresh_ms = 0;
static uint32_t last_network_error_ms = 0;
static bool dht_error_notified = false;
static uint32_t alert_until_ms = 0;
static char alert_text[17] = {};

static uint32_t time_ms() {

    return static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);

}

static void write_lcd_row(int row, const char* text) {

    char filled[17];
    int index = 0;

    while (text[index] && index < 16) {
        filled[index] = text[index];
        index++;
    }

    while (index < 16) {
        filled[index] = ' ';
        index++;
    }

    filled[16] = '\0';
    lcd.setCursor(0, row);
    lcd.print(filled);

}

static void change_screen(screen_mode next) {

    screen = next;
    screen_dirty = true;
    lcd.clear();

}

static void notify_user(const char* message, uint32_t duration_ms = 2100) {

    snprintf(alert_text, sizeof(alert_text), "%-16.16s", message);
    alert_until_ms = time_ms() + duration_ms;
    screen_dirty = true;

}

static bool get_targets(int id, float &start, float &stop) {

    switch (id) {

        case 0:
            start = program0.s_temp;
            stop = program0.p_temp;
            break;

        case 1:
            start = program1.get_stemp();
            stop = program1.get_ptemp();
            break;

        case 2:
            start = program2.get_stemp();
            stop = program2.get_ptemp();
            break;

        case 3:
            start = program3.get_stemp();
            stop = program3.get_ptemp();
            break;

        case 4:
            start = program4.get_stemp();
            stop = program4.get_ptemp();
            break;

        case 5:
            start = confort_mode.get_stemp();
            stop = confort_mode.get_ptemp();
            break;

        default:
            return false;

    }

    return true;

}

static uint32_t get_hours(int id) {

    switch (id) {

        case 1:
            return program1.allowed_hours;

        case 2:
            return program2.allowed_hours;

        case 3:
            return program3.allowed_hours;

        case 4:
            return program4.allowed_hours;

        default:
            return 0x00FFFFFFUL;

    }

}

static bool permitted_to_heat(int id, int current_hour) {

    if (id == 0 || id == 5) {
        return true;
    }

    if (!time_initialized || current_hour < 0 || current_hour > 23) {
        return false;
    }

    return (get_hours(id) & (1UL << current_hour)) != 0;

}

static void save_program_editor() {

    // The basic P0 is a fixed default; P5 (Comfort) has temperature setpoints.
    switch (selected_program) {

        case 1:
            program1.set_stemp(draft_start_temp);
            program1.set_ptemp(draft_stop_temp);
            program1.allowed_hours = draft_hours;
            break;
        case 2:
            program2.set_stemp(draft_start_temp);
            program2.set_ptemp(draft_stop_temp);
            program2.allowed_hours = draft_hours;
            break;
        case 3:
            program3.set_stemp(draft_start_temp);
            program3.set_ptemp(draft_stop_temp);
            program3.allowed_hours = draft_hours;
            break;
        case 4:
            program4.set_stemp(draft_start_temp);
            program4.set_ptemp(draft_stop_temp);
            program4.allowed_hours = draft_hours;
            break;
        case 5:
            confort_mode.set_stemp(draft_start_temp);
            confort_mode.set_ptemp(draft_stop_temp);
            break;

    }

    program_choice = selected_program;

    if (time_initialized) {
        read_time(hour, minute, day);
    }

    storage.save();
    change_screen(SCREEN_MENU);
    notify_user("PROGRAM SAVED");

}

static void save_time_editor() {

    set_time(draft_hour, draft_minute, draft_day);
    time_initialized = true;
    read_time(hour, minute, day);
    storage.save();
    change_screen(SCREEN_MENU);
    notify_user("TIME SAVED");

}

static void save_daynight_editor() {

    day_start = draft_day_start;
    night_start = draft_night_start;

    if (time_initialized) {
        read_time(hour, minute, day);
    }

    storage.save();
    change_screen(SCREEN_MENU);
    notify_user("DAY/NIGHT SAVED");

}

static void begin_program_edit() {

    selected_program = selected_program < 1 ? 1 : selected_program;
    get_targets(selected_program, draft_start_temp, draft_stop_temp);
    draft_hours = get_hours(selected_program);
    edit_field = 0;
    selected_hour = 0;
    change_screen(SCREEN_PROGRAM_EDIT);

}

static void begin_time_edit() {

    if (time_initialized) {
        read_time(hour, minute, day);
    }

    draft_hour = hour;
    draft_minute = minute;
    draft_day = day;
    edit_field = 0;
    change_screen(SCREEN_TIME_EDIT);

}

static void begin_daynight_edit() {

    draft_day_start = day_start;
    draft_night_start = night_start;
    edit_field = 0;
    change_screen(SCREEN_DAYNIGHT_EDIT);

}

static void menu_selection() {

    switch (menu_index) {

        case 0:
            selected_program = program_choice;
            change_screen(SCREEN_PROGRAM_SELECT);
            break;

        case 1:
            selected_program = program_choice == 0 ? 1 : program_choice;
            change_screen(SCREEN_PROGRAM_PICK);
            break;

        case 2:
            begin_time_edit();
            break;

        case 3:
            begin_daynight_edit();
            break;

        case 4:
            manual_control = !manual_control;
            manual_heat = false;  // never activate heating just by entering manual mode
            notify_user(manual_control ? "MANUAL MODE ON" : "MANUAL MODE OFF");
            break;

        case 5:
            if (manual_control) {
                manual_heat = !manual_heat;
                notify_user(manual_heat ? "MANUAL HEAT ON" : "MANUAL HEAT OFF");
            } else {
                notify_user("ENABLE MANUAL 1ST");
            }
            break;

        case 6:
            change_screen(SCREEN_MAIN);
            break;

    }

}

static void modify_program_temp(int change) {

    const float step = 0.5f * change;

    if (edit_field == 0) {
        draft_start_temp += step;
        if (draft_start_temp < 5.0f) draft_start_temp = 5.0f;
        if (draft_start_temp > 34.5f) draft_start_temp = 34.5f;
        if (draft_stop_temp < draft_start_temp + 0.5f) {
            draft_stop_temp = draft_start_temp + 0.5f;
        }
    } else if (edit_field == 1) {
        draft_stop_temp += step;
        if (draft_stop_temp < draft_start_temp + 0.5f) {
            draft_stop_temp = draft_start_temp + 0.5f;
        }
        if (draft_stop_temp > 35.0f) draft_stop_temp = 35.0f;
    }

}

static int wrapped_value(int value, int lower, int upper) {

    if (value < lower) return upper;
    if (value > upper) return lower;
    return value;

}

static void handle_short_button(int which) {

    // which: 0 = UP, 1 = DOWN, 2 = MENU, 3 = SAVE
    const int direction = which == 0 ? 1 : -1;

    switch (screen) {

        case SCREEN_MAIN:
            if (which <= 1) {
                home_page = 1 - home_page;
            } else if (which == 2) {
                change_screen(SCREEN_MENU);
            }
            break;

        case SCREEN_MENU:
            if (which <= 1) {
                menu_index = wrapped_value(menu_index - direction, 0, 6);
            } else if (which == 2) {
                menu_selection();
            } else {
                change_screen(SCREEN_MAIN);
            }
            break;

        case SCREEN_PROGRAM_SELECT:
            if (which <= 1) {
                selected_program = wrapped_value(selected_program + direction, 0, 5);
            } else if (which == 2) {
                program_choice = selected_program;
                if (time_initialized) read_time(hour, minute, day);
                storage.save();
                change_screen(SCREEN_MENU);
                notify_user("PROGRAM SELECTED");
            } else {
                change_screen(SCREEN_MENU);
            }
            break;

        case SCREEN_PROGRAM_PICK:
            if (which <= 1) {
                selected_program = wrapped_value(selected_program + direction, 1, 5);
            } else if (which == 2) {
                begin_program_edit();
            } else {
                change_screen(SCREEN_MENU);
            }
            break;

        case SCREEN_PROGRAM_EDIT:
            if (which <= 1 && edit_field <= 1) {
                modify_program_temp(direction);
            } else if (which == 2) {
                if (edit_field == 2) {
                    change_screen(SCREEN_HOUR_EDIT);
                } else {
                    edit_field++;
                    if (selected_program == 5 && edit_field > 1) {
                        edit_field = 0;
                    }
                }
            } else if (which == 3) {
                save_program_editor();
            }
            break;

        case SCREEN_HOUR_EDIT:
            if (which <= 1) {
                selected_hour = wrapped_value(selected_hour + direction, 0, 23);
            } else if (which == 2) {
                draft_hours ^= (1UL << selected_hour);
            } else {
                change_screen(SCREEN_PROGRAM_EDIT);
            }
            break;

        case SCREEN_TIME_EDIT:
            if (which <= 1) {
                if (edit_field == 0) {
                    draft_hour = wrapped_value(draft_hour + direction, 0, 23);
                } else if (edit_field == 1) {
                    draft_minute = wrapped_value(draft_minute + direction, 0, 59);
                } else {
                    draft_day = wrapped_value(draft_day + direction, 1, 7);
                }
            } else if (which == 2) {
                edit_field = (edit_field + 1) % 3;
            } else {
                save_time_editor();
            }
            break;

        case SCREEN_DAYNIGHT_EDIT:
            if (which <= 1) {
                if (edit_field == 0) {
                    draft_day_start = wrapped_value(draft_day_start + direction, 0, 23);
                } else {
                    draft_night_start = wrapped_value(draft_night_start + direction, 0, 23);
                }
            } else if (which == 2) {
                edit_field = (edit_field + 1) % 2;
            } else {
                save_daynight_editor();
            }
            break;

    }

    screen_dirty = true;

}

static void scan_buttons(uint32_t now) {

    uint8_t up = key_up.update(now);
    uint8_t down = key_down.update(now);
    uint8_t menu = key_menu.update(now);
    uint8_t save = key_save.update(now);

    if (up || down || menu || save) {
        last_button_ms = now;
    }

    if (menu == 2) {
        // Leaving any edit without saving discards its draft.
        change_screen(screen == SCREEN_MAIN ? SCREEN_MENU : SCREEN_MAIN);
        return;
    }

    if (up == 1 || up == 3) handle_short_button(0);
    if (down == 1 || down == 3) handle_short_button(1);
    if (menu == 1) handle_short_button(2);
    if (save == 1) handle_short_button(3);

    // Original prototype behavior: save edits after 10 seconds of inactivity.
    if ((uint32_t)(now - last_button_ms) >= 10000) {

        if (screen == SCREEN_PROGRAM_EDIT || screen == SCREEN_HOUR_EDIT) {
            save_program_editor();
            last_button_ms = now;
        } else if (screen == SCREEN_TIME_EDIT) {
            save_time_editor();
            last_button_ms = now;
        } else if (screen == SCREEN_DAYNIGHT_EDIT) {
            save_daynight_editor();
            last_button_ms = now;
        }

    }

}

static const char* menu_items[7] = {

    "SEL PROGRAM",
    "EDIT PROGRAM",
    "SET TIME/DAY",
    "DAY/NIGHT HOURS",
    "MANUAL MODE",
    "MANUAL HEAT",
    "EXIT"

};

static void draw_screen(uint32_t now) {

    if (!screen_dirty && (uint32_t)(now - last_draw_ms) < 450) {
        return;
    }

    screen_dirty = false;
    last_draw_ms = now;

    char top[32] = {};
    char bottom[32] = {};

    if ((int32_t)(alert_until_ms - now) > 0) {
        write_lcd_row(0, alert_text);
        write_lcd_row(1, "SAVE/BACK  MENU");
        return;
    }

    switch (screen) {

        case SCREEN_MAIN: {

            if (home_page == 0) {

                char temp_text[8] = "--.-";
                char humidity_text[6] = "--";
                char ext_text[8] = "--.-";

                if (sensor.isValid()) {
                    snprintf(temp_text, sizeof(temp_text), "%4.1f", sensor.getTemperature());
                    snprintf(humidity_text, sizeof(humidity_text), "%2.0f", sensor.getHumidity());
                }

                if (local::instance && local::instance->externalAvailable()) {
                    snprintf(ext_text, sizeof(ext_text), "%4.1f", ext_temp);
                }

                snprintf(top, sizeof(top), "T%s H%s X%s", temp_text, humidity_text, ext_text);

                char boiler = '?';
                if (local::instance && local::instance->connected()) {
                    boiler = local::instance->actualRelayOn() ? '1' : '0';
                }

                if (time_initialized) {
                    snprintf(bottom, sizeof(bottom), "%02d:%02d %d P%d B%c M%d",
                             hour, minute, day, program_choice, boiler,
                             manual_control ? 1 : 0);
                } else {
                    snprintf(bottom, sizeof(bottom), "--:-- - P%d B%c M%d",
                             program_choice, boiler, manual_control ? 1 : 0);
                }

            } else {

                const bool link_ok = local::instance && local::instance->connected();
                const bool su_manual = local::instance && local::instance->remoteManualOn();
                const bool allowed = permitted_to_heat(program_choice, hour);

                snprintf(top, sizeof(top), "LINK:%s SU-M:%d",
                         link_ok ? "OK " : "LOST", su_manual ? 1 : 0);
                bool daytime = (day_start < night_start) ?
                    (hour >= day_start && hour < night_start) :
                    (hour >= day_start || hour < night_start);
                char day_night = time_initialized ? (daytime ? 'D' : 'N') : '?';
                snprintf(bottom, sizeof(bottom), "P%d H:%s %c C:%d",
                         program_choice, allowed ? "ON " : "OFF",
                         day_night, last_command ? 1 : 0);

            }
            break;
        }

        case SCREEN_MENU:
            snprintf(top, sizeof(top), ">%s", menu_items[menu_index]);
            snprintf(bottom, sizeof(bottom), " %s", menu_items[(menu_index + 1) % 7]);
            break;

        case SCREEN_PROGRAM_SELECT:
            snprintf(top, sizeof(top), "SELECT PROGRAM");
            snprintf(bottom, sizeof(bottom), "< P%d  MENU=OK >", selected_program);
            break;

        case SCREEN_PROGRAM_PICK:
            snprintf(top, sizeof(top), "EDIT WHICH PROG?");
            snprintf(bottom, sizeof(bottom), "< P%d  MENU=OK >", selected_program);
            break;

        case SCREEN_PROGRAM_EDIT:
            if (edit_field == 0) {
                snprintf(top, sizeof(top), "P%d START: %4.1fC", selected_program, draft_start_temp);
                snprintf(bottom, sizeof(bottom), "UP/DN  M=NEXT");
            } else if (edit_field == 1) {
                snprintf(top, sizeof(top), "P%d STOP:  %4.1fC", selected_program, draft_stop_temp);
                snprintf(bottom, sizeof(bottom), "UP/DN  M=NEXT");
            } else {
                snprintf(top, sizeof(top), "P%d HOURS EDIT", selected_program);
                snprintf(bottom, sizeof(bottom), "MENU=ENTER");
            }
            break;

        case SCREEN_HOUR_EDIT:
            snprintf(top, sizeof(top), "P%d %02d:00 %s", selected_program, selected_hour,
                     (draft_hours & (1UL << selected_hour)) ? "ON" : "OFF");
            snprintf(bottom, sizeof(bottom), "UP/DN M=TOGGLE");
            break;

        case SCREEN_TIME_EDIT:
            snprintf(top, sizeof(top), "SET %02d:%02d DAY:%d",
                     draft_hour, draft_minute, draft_day);
            snprintf(bottom, sizeof(bottom), "EDIT:%s", edit_field == 0 ? "HOUR" :
                     (edit_field == 1 ? "MINUTE" : "DAY"));
            break;

        case SCREEN_DAYNIGHT_EDIT:
            snprintf(top, sizeof(top), "DAY:%02d NIGHT:%02d",
                     draft_day_start, draft_night_start);
            snprintf(bottom, sizeof(bottom), "EDIT:%s", edit_field == 0 ? "DAY" : "NIGHT");
            break;

    }

    write_lcd_row(0, top);
    write_lcd_row(1, bottom);

}

static void update_lcd_brightness(uint32_t now) {

    if ((uint32_t)(now - last_light_ms) < 1000) {
        return;
    }

    last_light_ms = now;
    sensor.readPhotoresistorValue();

    const int raw = sensor.getLight();
    uint32_t duty = 50;

    if (raw >= ULTRA_H_BC) duty = 255;
    else if (raw >= ULTRA_BC) duty = 230;
    else if (raw >= HIGH_BC) duty = 205;
    else if (raw >= INTER_NC) duty = 175;
    else if (raw >= MEDIUM_BC) duty = 145;
    else if (raw >= LOW_H_BC) duty = 115;
    else if (raw >= LOW_BC) duty = 85;

    sensor.setBrightness(duty);

}

static void update_heating_control(uint32_t now) {

    if ((uint32_t)(now - last_control_ms) < 1000) {
        return;
    }

    last_control_ms = now;

    if (time_initialized) {
        read_time(hour, minute, day);
    }

    bool demand = false;

    // Entering Interface [B] stops automatic heating until the user exits.
    // This is deliberately fail-safe: don't hold a stale ON command in menus.
    if (screen == SCREEN_MAIN) {

        if (manual_control) {
            demand = manual_heat;
        } else if (sensor.isValid() &&
                   permitted_to_heat(program_choice, hour)) {

            float start = 20.0f;
            float stop = 23.0f;
            get_targets(program_choice, start, stop);

            demand = last_command;

            if (check_for_start(sensor.getTemperature(), start)) {
                demand = true;
            }

            if (check_for_stop(sensor.getTemperature(), stop)) {
                demand = false;
            }

        }

    }

    if (demand != last_command ||
        (uint32_t)(now - last_command_refresh_ms) >= 10000) {
        if (local::instance) {
            local::instance->send(demand);
        }
        last_command_refresh_ms = now;
    }

}

static void process_interface(uint32_t now) {

    if (local::instance) {
        local::instance->process();
        if (local::instance->errorCode() &&
            local::instance->errorTimeMs() != last_network_error_ms) {

            last_network_error_ms = local::instance->errorTimeMs();

            switch (local::instance->errorCode()) {
                case 1: notify_user("ESP-NOW SEND ERR"); break;
                case 2: notify_user("ESP-NOW RX ERROR"); break;
                case 3: notify_user("SU WATCHDOG RESET"); break;
                case 4: notify_user("SU VOLTAGE DROP"); break;
                case 5: notify_user("SU RESTARTED"); break;
                case 6: notify_user("SU STATUS ERROR"); break;
                case 7: notify_user("SU NTC SENSOR ERR"); break;
            }

        }
    }

    if ((uint32_t)(now - last_sensor_ms) >= 2500) {

        last_sensor_ms = now;
        if (!sensor.readTemperatureHumidity()) {
            if (!dht_error_notified) {
                notify_user("DHT11 READ ERROR");
                dht_error_notified = true;
            }
        } else {
            dht_error_notified = false;
        }

    }

    scan_buttons(now);
    update_lcd_brightness(now);
    update_heating_control(now);
    draw_screen(now);

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

    // NVS must load the stored peer MAC before esp_now_add_peer() is called.
    esp_err_t nvs_result = nvs_flash_init();
    if (nvs_result == ESP_ERR_NVS_NO_FREE_PAGES ||
        nvs_result == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        nvs_result = nvs_flash_init();
    }
    ESP_ERROR_CHECK(nvs_result);
    storage.begin();

    // Wi-Fi + ESP-NOW 
    if (!command.begin()) {
        ESP_LOGE(TAG_TH, "ESP-NOW event queue creation failed");
        esp_restart();
    }
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

    // NVS / preferences (already restored before peer registration)

    // A software RTC cannot know how long power was off.
    // Keep last saved hour/minute/day as editor defaults, but require confirmation
    // before enabling scheduled P1-P4 heating after any restart.
    time_initialized = false;

    last_sensor_ms = time_ms() - 2500;
    last_light_ms = time_ms() - 1000;
    last_button_ms = time_ms();

    // Require a new safe command before the controller can start heating.
    command.send(false);

    delay_s(500); // stabilization

    splash_1();
    lcd.clear();
    splash_2();

    delay_s(1000);
    lcd.clear();

    while (true) {

        process_interface(time_ms());
        delay_s(30);

    }

}
