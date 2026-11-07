#pragma once

// IDF core
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_timer.h"
#include "esp_idf_version.h"
#include <math.h>
#include <string.h>
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

// 16-byte ESP-NOW Local Master Key. MUST be identical on both boards.
/*!!! WARNING, THIS IS TO BE USED ONLY FOR TESTING, FOR DEPLOYEMENT ALWAYS GENERATE A NEW ONE !!!*/
static const uint8_t ESP_NOW_LMK[16] = { 0xDE, 0x24, 0x64, 0x98, 0x81, 0x22, 0x9E, 0xC9, 0xC9, 0xAD, 0x66, 0x90, 0x04, 0x64, 0xE0, 0x48 };

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

    // Set LCD backlight brightness (0-255), 8bit res;
    void setBrightness(uint32_t value) {

        ledc_set_duty(LEDC_SPEED_DISPLAY, LEDC_CH_DISPLAY, value);
        ledc_update_duty(LEDC_SPEED_DISPLAY, LEDC_CH_DISPLAY);

    }

    bool readTemperatureHumidity() {

        float h = dht.readHumidity();
        float t = dht.readTemperature();

        if (isnan(h) || isnan(t)) {

            ESP_LOGW(TAG_TH, "DHT11 read failed!");
            valid = false;
            return false;

        }

        tempDHT11     = t;
        humidityDHT11 = h;
        valid = true;
        return true;

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
    bool  isValid()        const { return valid;         }

private:

    float tempDHT11     = 0.0f;
    float humidityDHT11 = 0.0f;
    int   light         = 0;
    bool  valid         = false;

} sensor;


// ERROR CODES  (shared between both units — keep in sync with
// the matching enum in main.cpp on the Starting Unit
typedef enum : uint8_t 
{

    UNIT_CK            = 0,
    MANUAL_OVERRIDE    = 1,
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
//    SU  → TH : esp_data_err&sensors { UNIT_ERR, SENSOR_CK, EXT_TEMP_VAL }
// ============================================================

// ESP-NOW callbacks only enqueue events. Never delay or use LCD in the Wi-Fi task.
class local {

public:

    static local* instance;

    // Creates the communication queue before ESP-NOW callbacks are registered.
    bool begin() {

        events = xQueueCreate(16, sizeof(net_event));
        return events != nullptr;

    }

    static void onReceive(const esp_now_recv_info_t* recv_info, const uint8_t* data, int len) {

        if (instance) {
            instance->onReceiveFinal(recv_info, data, len);
        }

    }

    // IDF 5.5+ logic
    static void onSent(const esp_now_send_info_t* send_info, esp_now_send_status_t status) {

        (void)send_info;

        if (instance) {
            instance->onSentFinal(status);
        }

    }

    void onReceiveFinal(const esp_now_recv_info_t* recv_info, const uint8_t* data, int len) {

        // memory check 
        if (!events || !recv_info || !recv_info->src_addr || memcmp(recv_info->src_addr, receiverMAC, sizeof(receiverMAC)) != 0) {
            return;
        }

        net_event event = {};
        event.type = RX_INVALID;

        // size check 
        if (data && len == (int)sizeof(esp_data_err_sensor)) {

            event.type = RX_STATUS;
            memcpy(&event.packet, data, sizeof(event.packet));

        }

        (void)xQueueSend(events, &event, 0);

    }

    void onSentFinal(esp_now_send_status_t status) {

        if (!events) {
            return;
        }

        net_event event = {};
        event.type = (status == ESP_NOW_SEND_SUCCESS) ? TX_SUCCESS : TX_FAILED;
        (void)xQueueSend(events, &event, 0);

    }

    // public send (enqueue a desired boiler state; process() performs actual transmit)
    void send(bool start) {

        desired_command = start;
        last_command    = start;
        queued_command  = true;

        if (!busy) {
            retry_count = 0;
            retry_after_ms = 0;
        }

    }

    void process() {

        const uint32_t now = static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);
        net_event event = {};

        while (events && xQueueReceive(events, &event, 0) == pdTRUE) {

            switch (event.type) {

                case RX_STATUS:
                    failed_receive = 0;
                    status_received = true;
                    last_rx_ms = now;
                    relay_on = event.packet.RELAY_ON != 0;
                    remote_manual = event.packet.MANUAL_ON != 0;

                    if (event.packet.SENSOR_CK == 1 &&
                        isfinite(event.packet.EXT_TEMP_VAL)) {
                        ext_temp = event.packet.EXT_TEMP_VAL;
                        external_valid = true;
                    } else {
                        external_valid = false;
                        if (event.packet.SENSOR_CK == 9) {
                            show_error(7);
                        }
                    }

                    switch (event.packet.UNIT_ERR) {

                        case UNIT_CK:
                            break;
                        case MANUAL_OVERRIDE:
                            remote_manual = true;
                            break;
                        case ERR_WATCHDOG_RESET:
                            show_error(3);
                            break;
                        case ERR_BROWNOUT:
                            show_error(4);
                            break;
                        case ERR_REBOOTED:
                            show_error(5);
                            break;
                        default:
                            show_error(6);
                            break;

                    }
                    break;

                case RX_INVALID:
                    failed_receive++;
                    show_error(2);
                    break;

                case TX_SUCCESS:
                    busy = false;
                    counter = 0;
                    retry_count = 0;
                    // A new state may have been queued during the previous transmission.
                    break;

                case TX_FAILED:
                    busy = false;
                    counter++;
                    retry_count++;

                    if (retry_count <= 10) {
                        queued_command = true;
                        retry_after_ms = now + 500;
                    } else {
                        queued_command = false;
                        show_error(1);
                    }
                    break;

            }

        }

        // A missing callback should not block communication forever.
        if (busy && (uint32_t)(now - send_started_ms) >= 1500) {

            busy = false;
            retry_count++;
            queued_command = retry_count <= 10;
            retry_after_ms = now + 500;

            if (!queued_command) {
                show_error(1);
            }
        }

        // Only one packet at a time; the 500 ms retry belongs here, NOT in callbacks.
        if (!busy && queued_command && (int32_t)(now - retry_after_ms) >= 0) {

            data_send_pkt.execute = desired_command;
            esp_err_t result = esp_now_send(receiverMAC, (const uint8_t*)&data_send_pkt, sizeof(data_send_pkt));

            if (result == ESP_OK) {
                busy = true;
                queued_command = false;
                send_started_ms = now;
            } else {
                retry_count++;
                retry_after_ms = now + 500;
                if (retry_count > 10) {
                    queued_command = false;
                    show_error(1);
                }
            }

        }

    }

    bool connected() const {

        uint32_t now = static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);
        return status_received && (uint32_t)(now - last_rx_ms) <= 35000;

    }

    bool actualRelayOn()     const { return relay_on; }
    bool remoteManualOn()    const { return remote_manual; }
    bool externalAvailable() const { return external_valid && connected(); }
    int errorCode()          const { return current_error; }
    uint32_t errorTimeMs()   const { return error_started_ms; }

private:

    // Outgoing: command to Starting_Unit
    struct [[gnu::packed]] esp_data_send {
        bool execute;
    };

    // Incoming: external temperature and relay state from Starting Unit
    // Appended two status bytes; old 6-byte packet is intentionally incompatible.
    struct [[gnu::packed]] esp_data_err_sensor {
        uint8_t UNIT_ERR;
        uint8_t SENSOR_CK;
        float   EXT_TEMP_VAL;
        uint8_t RELAY_ON;
        uint8_t MANUAL_ON;
    };

    static_assert(sizeof(esp_data_send) == 1, "Unexpected command packet size");
    static_assert(sizeof(esp_data_err_sensor) == 8, "Unexpected status packet size");

    enum event_type : uint8_t {
        RX_STATUS,
        RX_INVALID,
        TX_SUCCESS,
        TX_FAILED
    };

    struct net_event {
        event_type type;
        esp_data_err_sensor packet;
    };

    void show_error(int code) {

        current_error = code;
        error_started_ms = static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);

    }

    QueueHandle_t events = nullptr;
    esp_data_send data_send_pkt = {};
    bool queued_command  = false;
    bool desired_command = false;
    bool busy            = false;
    bool status_received = false;
    bool external_valid  = false;
    bool relay_on        = false;
    bool remote_manual   = false;
    int retry_count      = 0;
    int current_error    = 0;
    uint32_t retry_after_ms  = 0;
    uint32_t send_started_ms = 0;
    uint32_t last_rx_ms      = 0;
    uint32_t error_started_ms= 0;

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
    peer.ifidx = WIFI_IF_STA;
    peer.encrypt = true; // AES128 encrypt
    memcpy(peer.lmk, ESP_NOW_LMK, sizeof(peer.lmk));
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