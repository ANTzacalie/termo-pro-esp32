#pragma once

// IDF core
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_adc/adc_oneshot.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_now.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "nvs.h"

// time
#include <time.h>
#include <sys/time.h>

#include "DHT.h"           // Adafruit DHT sensor library
#include "LiquidCrystal.h" // Arduino LiquidCrystal (4-bit parallel)

// project header
#include "thermostat_var.h"

static const char* TAG_TH = "LOG[TH]";

// ============================================================
//  DHT11
// ============================================================
#define DHTPIN  GPIO_NUM_21
#define DHTTYPE DHT11
DHT dht(DHTPIN, DHTTYPE);

// ============================================================
//  LCD1602A  —  4-bit parallel
//  Pins: RS=18, E=19, D4=4, D5=14, D6=16, D7=17
// ============================================================
LiquidCrystal lcd(18, 19, 4, 14, 16, 17);

// Backlight PMW DutyCycle etc...
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


static void lcd_row_sel(int select) {

    lcd.setCursor(0 , select);

}

static void clear_char_pos(int col, int row) {

    lcd.setCursor(col, row);
    lcd.print(" ");

}

static void delay_s(uint32_t ms) {

    vTaskDelay(pdMS_TO_TICKS(ms));

}

// LDR handler
static adc_oneshot_unit_handle_t adc1_handle;

// LDR init
static void adc_ldr_init() {

    adc_oneshot_unit_init_cfg_t init_cfg = {
        .unit_id = ADC_UNIT_1,
    };
    adc_oneshot_new_unit(&init_cfg, &adc1_handle);

    adc_oneshot_chan_cfg_t chan_cfg = {
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_12,
    };
    adc_oneshot_config_channel(adc1_handle, ADC_CHANNEL_6, &chan_cfg);

}

class sensors {

public:

    // Set LCD backlight brightness (0-255)
    void setBrightness(uint32_t value) {

        ledc_set_duty(LEDC_SPEED_DISPLAY, LEDC_CH_DISPLAY, value);
        ledc_update_duty(LEDC_SPEED_DISPLAY, LEDC_CH_DISPLAY);

    }

    void readTemperatureHumidity() {

        float h = dht.readHumidity();
        float t = dht.readTemperature();

        if (isnan(h) || isnan(t)) {

            lcd_row_sel(0);
            lcd.print("TEMP_READ_ERR");
            ESP_LOGE(TAG_TH, "DHT11 read failed");
            return;

        }

        tempDHT11     = t;
        humidityDHT11 = h;

    }

    // Read raw 12-bit ADC value from LDR (GPIO34 / ADC1_CH6)
    void readPhotoresistorValue() {

        int raw = 0;
        adc_oneshot_read(adc1_handle, ADC_CHANNEL_6, &raw);
        light = raw;

    }

    float getTemperature() const { return tempDHT11;     }
    float getHumidity()    const { return humidityDHT11; }
    int   getLight()       const { return light;         }

private:

    float tempDHT11     = 0.0f;
    float humidityDHT11 = 0.0f;
    int   light         = 0;

} sensor;


// ERROR CODES  (shared between both units — keep in sync with
// the matching enum in main.cpp on the Starting Unit)
typedef enum : uint8_t 
{

    UNIT_CK            = 0,
    // communication
    ERR_LEN_INVALID    = 10,
    ERR_DATA_INVALID   = 11,
    // system
    ERR_BROWNOUT       = 30,
    ERR_REBOOTED       = 31,
    ERR_WATCHDOG_RESET = 32

} UNIT_P_ERR;


// ============================================================
//  ESP-NOW COMMUNICATION  (Thermostat Unit side)
//
//  Packet flow:
//    TH  → SU : esp_data_send   { bool execute }
//    SU  → TH : esp_data_state  { SETUP_UNIT_CK, UNIT_ERR  }
//    SU  → TH : esp_data_sensor { SENSOR_CK, EXT_TEMP_VAL  }  [TODO]
// ============================================================

class local {

public:

    static local* instance;

    static void onReceive(const esp_now_recv_info_t* recv_info, const uint8_t* data, int len)
    {
        (void)recv_info;
        if (instance) instance->onReceiveFinal(data, len);
    }

    // IDF 5.x send callback
    static void onSent(const esp_now_send_info_t* send_info,esp_now_send_status_t status)
    {
        if (instance) instance->onSentFinal(*send_info, status);
    }

    // receive handler
    void onReceiveFinal(const uint8_t* data, int len) {

        // Handle esp_data_state packet (SU boot / error report)
        if (len == (int)sizeof(esp_data_state)) {

            esp_data_state pkt;
            memcpy(&pkt, data, sizeof(pkt));

            if (pkt.SETUP_UNIT_CK) {

                switch (pkt.UNIT_ERR) {

                    case UNIT_CK:
                        lcd.clear();
                        lcd_row_sel(0);
                        lcd.print("SU CONNECTED");
                        break;

                    case ERR_WATCHDOG_RESET:
                        lcd.clear();
                        lcd_row_sel(0);
                        lcd.print("RESTART SU NOW!");
                        break;

                    case ERR_BROWNOUT:
                        lcd.clear();
                        lcd_row_sel(0);
                        lcd.print("DROP.VOLT ERR SU");
                        break;

                    default:
                        lcd.clear();
                        lcd_row_sel(0);
                        lcd.print("UNKNOWN ERROR SU");
                        break;

                }

            }

            failed_receive = 0;
            return;

        }

        // TODO: handle esp_data_sensor packet when SU external sensor is implemented
        // if (len == (int)sizeof(esp_data_sensor)) { ... }

        // Unknown / wrong-length packet
        if (failed_receive < 10) {

            failed_receive++;

        } else {

            send(false);
            esp_restart();

        }

    }

    // send-confirm handler
    void onSentFinal(const esp_now_send_info_t, esp_now_send_status_t status) {

        if (status == ESP_NOW_SEND_SUCCESS) {

            counter = 0;

        } else if (counter < 10) {

            counter++;
            send(last_command);

        } else {

            ESP_LOGE(TAG_TH, "ESP-NOW send retry limit — restarting");
            esp_restart();

        }

    }

    // public send 
    void send(bool start) {

        data_send_pkt.execute = start;
        last_command = start;
        esp_now_send(receiverMAC, (const uint8_t*)&data_send_pkt, sizeof(data_send_pkt));

    }

private:

    // Outgoing: command to Starting_Unit
    struct [[gnu::packed]] esp_data_send {
        bool execute;
    };

    // Incoming: boot / error status from Starting Unit
    struct [[gnu::packed]] esp_data_state {
        bool    SETUP_UNIT_CK;
        uint8_t UNIT_ERR;
    };

    // Incoming: external temperature from Starting Unit (TODO — not implemented on SU yet)
    struct [[gnu::packed]] esp_data_sensor {
        uint8_t SENSOR_CK;
        float   EXT_TEMP_VAL;
    };

    esp_data_send data_send_pkt = {};

} command_now;

static void wifi_espnow_init() {

    // NVS storage check
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    // check for TCP/IP stack errors and init
    ESP_ERROR_CHECK(esp_netif_init());

    // checks for memory loop errors and init
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // WIFI init and error check
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

    // registers onReceive and onSent from class local
    ESP_ERROR_CHECK(esp_now_register_recv_cb(local::onReceive));
    ESP_ERROR_CHECK(esp_now_register_send_cb(local::onSent));

    // Peer setup, channel and enctyption[-- AES128 based --]
    esp_now_peer_info_t peer = {};
    memcpy(peer.peer_addr, receiverMAC, 6);
    peer.channel = 0;
    peer.encrypt = true;
    ESP_ERROR_CHECK(esp_now_add_peer(&peer));

}

static void gpio_buttons_init() {

    // UP
    gpio_set_direction(BUTTON_UP_PIN  , GPIO_MODE_INPUT );
    gpio_set_pull_mode(BUTTON_UP_PIN  , GPIO_PULLUP_ONLY);

    // DOWN
    gpio_set_direction(BUTTON_DOWN_PIN, GPIO_MODE_INPUT );
    gpio_set_pull_mode(BUTTON_DOWN_PIN, GPIO_PULLUP_ONLY);

    // MENIU
    gpio_set_direction(BUTTON_MENU_PIN, GPIO_MODE_INPUT );
    gpio_set_pull_mode(BUTTON_MENU_PIN, GPIO_PULLUP_ONLY);

    // SAVE
    gpio_set_direction(BUTTON_SAVE_PIN, GPIO_MODE_INPUT );
    gpio_set_pull_mode(BUTTON_SAVE_PIN, GPIO_PULLUP_ONLY);

}


//  NVS STORAGE  (Preferences wrapper — bodies stubbed, fill later)
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

        //   TODO: persist program temps, day_start, night_start, comfort_factor
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

        //   TODO: restore saved values from NVS
        //   example:
        //   int32_t v = 5;
        //   nvs_get_i32(nvs_handle, "day_start", &v);
        //   day_start = v;
        //   ...

    }

} storage;