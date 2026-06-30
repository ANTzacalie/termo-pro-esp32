#include "thermostat.h"

// ============================================================
//  GLOBAL DEFINITIONS  (extern declarations live in thermostat_var.h)
// ============================================================

// retry & safety
int  counter        = 0;
int  failed_receive = 0;
bool last_command   = false;

// receiver MAC — Starting Unit (replace with real MAC before flashing!)
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
//local* local::instance = nullptr;


// ============================================================
//  PROGRAM INSTANCES
// ============================================================
auto_mode program_auto;
prog_1    program1;
prog_2    program2;
prog_3    program3;
prog_4    program4;
basic     program0;


// ============================================================
//  LCD BACKLIGHT BRIGHTNESS THRESHOLDS  (12-bit ADC values)
// ============================================================
enum brightness_control : uint16_t {

    LOW_BC    = 1200,
    LOW_H_BC  = 1800,
    MEDIUM_BC = 2400,
    INTER_NC  = 2900,
    HIGH_BC   = 3400,
    ULTRA_BC  = 3800,
    ULTRA_H_BC = 4000

};


// ============================================================
//  SOFT-RTC HELPERS
// ============================================================

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
    weekday = (ti.tm_wday == 0) ? 7 : ti.tm_wday; // Sun=0 → 7

}


// ============================================================
//  START / STOP LOGIC
// ============================================================

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


// ============================================================
//  CUSTOM LCD CHARACTERS
// ============================================================

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

static void clearArrowAt(int col, int row) {

    lcd.setCursor(col, row);
    lcd.print(" ");

}


// ============================================================
//  NVS STORAGE  (Preferences wrapper — bodies stubbed, fill later)
// ============================================================
class data {

private:
    nvs_handle_t nvs_handle = 0;

public:

    // Call once at boot — opens NVS namespace and loads saved values.
    void begin() {

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

        // TODO: persist program temps, day_start, night_start, comfort_factor
        //   example:
        //   nvs_set_i32(nvs_handle, "day_start",   day_start);
        //   nvs_set_i32(nvs_handle, "night_start",  night_start);
        //   nvs_set_blob(nvs_handle, "p0_start", &program0.START_TEMP, sizeof(float));
        //   ...
        //   nvs_commit(nvs_handle);

        nvs_close(nvs_handle);

    }

private:

    void load() {

        // TODO: restore saved values from NVS
        //   example:
        //   int32_t v = 5;
        //   nvs_get_i32(nvs_handle, "day_start", &v);
        //   day_start = v;
        //   ...

    }

} storage;


// ============================================================
//  HARDWARE INIT HELPERS
// ============================================================

static void gpio_buttons_init() {

    // Button A
    gpio_set_direction(BUTTON_A_PIN, GPIO_MODE_INPUT);
    gpio_set_pull_mode(BUTTON_A_PIN, GPIO_PULLUP_ONLY);

    // Button B
    gpio_set_direction(BUTTON_B_PIN, GPIO_MODE_INPUT);
    gpio_set_pull_mode(BUTTON_B_PIN, GPIO_PULLUP_ONLY);

}

/*
static void adc_ldr_init() {

    // ADC1 width: 12-bit (matches original analogReadResolution(12))
    adc1_config_width(ADC_WIDTH_BIT_12);

    // GPIO34 = ADC1_CH6; 11 dB attenuation → full 0-3.3 V range
    // (matches original analogSetPinAttenuation(ldrPin, ADC_11db))
    adc1_config_channel_atten(LDR_ADC_CHANNEL, ADC_ATTEN_DB_12);

}
*/

static void ledc_backlight_init() {

    ledc_timer_config_t timer = {};
    timer.speed_mode      = LEDC_SPEED_DISPLAY;
    timer.timer_num       = LEDC_TIMER_DISPLAY;
    timer.duty_resolution = LEDC_RESOLUTION;
    timer.freq_hz         = LEDC_FREQ_HZ;
    timer.clk_cfg         = LEDC_AUTO_CLK;
    ledc_timer_config(&timer);

    ledc_channel_config_t ch = {};
    ch.speed_mode = LEDC_SPEED_DISPLAY;
    ch.channel    = LEDC_CH_DISPLAY;
    ch.timer_sel  = LEDC_TIMER_DISPLAY;
    ch.gpio_num   = PWM_PIN_DISPLAY;
    ch.duty       = 0;
    ch.hpoint     = 0;
    ledc_channel_config(&ch);

}

static void wifi_espnow_init() {

    // NVS required by Wi-Fi driver (may already be init'd; ignore if so)
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());

    // ---- ESP-NOW ----
    if (esp_now_init() != ESP_OK) {
        ESP_LOGE(TAG_TH, "esp_now_init failed — restarting");
        esp_restart();
    }

    // IDF 5.x receive callback uses esp_now_recv_info_t*
    //ESP_ERROR_CHECK(esp_now_register_recv_cb(local::onReceive));
    //ESP_ERROR_CHECK(esp_now_register_send_cb(local::onSent));

    esp_now_peer_info_t peer = {};
    memcpy(peer.peer_addr, receiverMAC, 6);
    peer.channel = 0;
    peer.encrypt = false;
    ESP_ERROR_CHECK(esp_now_add_peer(&peer));

}


// ============================================================
//  APP MAIN  (replaces Arduino setup() + loop())
// ============================================================
extern "C" void app_main() {

    // ---- static ESP-NOW handler instance ----
    //static local command;
    //local::instance = &command;

    // ---- peripheral init ----
    gpio_buttons_init();
    //adc_ldr_init();
    ledc_backlight_init();

    // ---- Wi-Fi + ESP-NOW ----
    wifi_espnow_init();

    // ---- DHT11 ----
    dht.begin();

    // ---- LCD ----
    lcd.begin(16, 2);
    lcd.clear();
    lcd.createChar(0, arrowUp);

    // ---- initial brightness ----
    sensor.setBrightness(150);

    // ---- NVS / preferences ----
    storage.begin();

    vTaskDelay(pdMS_TO_TICKS(500)); // stabilization

    // ---- splash screen ----
    lcd.setCursor(0, 0);
    lcd.print("TermoPro v2.0");
    lcd.setCursor(0, 1);
    lcd.print("by ANTzacalie");
    vTaskDelay(pdMS_TO_TICKS(3000));

    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("For bugs: Git");
    lcd.setCursor(0, 1);
    lcd.print("Sys: 1, WIFI: 1");
    vTaskDelay(pdMS_TO_TICKS(3000));

    lcd.clear();

    // ---- main task loop (TODO: add menu / button / display logic) ----
    while (true) {

        vTaskDelay(pdMS_TO_TICKS(10));
        lcd.print(sensor.getTemperature());

    }

}
