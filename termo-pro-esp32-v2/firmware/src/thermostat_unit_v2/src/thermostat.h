#pragma once

// ---- IDF core ----
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_now.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "nvs.h"

// ---- time ----
#include <time.h>
#include <sys/time.h>

// ---- kept Arduino-ecosystem libraries (user decision) ----
#include "DHT.h"           // Adafruit DHT sensor library
#include "LiquidCrystal.h" // Arduino LiquidCrystal (4-bit parallel)

// ---- project header ----
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


// ============================================================
//  SENSORS
// ============================================================
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

            lcd.setCursor(0, 0);
            lcd.print("TEMP_READ_ERR");
            ESP_LOGE(TAG_TH, "DHT11 read failed");
            return;

        }

        tempDHT11    = t;
        humidityDHT11 = h;

    }

    // Read raw 12-bit ADC value from LDR (GPIO34 / ADC1_CH6)
    void readPhotoresistorValue() {

        //light = adc1_get_raw(LDR_ADC_CHANNEL);

    }

    float getTemperature() const { return tempDHT11;     }
    float getHumidity()    const { return humidityDHT11; }
    int   getLight()       const { return light;         }

private:

    float tempDHT11     = 0.0f;
    float humidityDHT11 = 0.0f;
    int   light         = 0;

} sensor;


// ============================================================
//  ERROR CODES  (shared between both units — keep in sync with
//  the matching enum in main.cpp on the Starting Unit)
// ============================================================
typedef enum : uint8_t {

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


/*
class local {

public:

    static local* instance;

    // ---- static trampoline callbacks (IDF 5.x signatures) ----

    static void onReceive(const esp_now_recv_info_t* recv_info,
                          const uint8_t* data, int len)
    {
        (void)recv_info;
        if (instance) instance->onReceiveFinal(data, len);
    }

    // IDF 5.x send callback — uses esp_now_send_info_t* (not raw mac pointer)
    static void onSent(const esp_now_send_info_t* send_info,esp_now_send_status_t status)
    {
        if (instance) instance->onSentFinal(send_info, status);
    }

    // ---- receive handler ----
    void onReceiveFinal(const uint8_t* data, int len) {

        // Handle esp_data_state packet (SU boot / error report)
        if (len == (int)sizeof(esp_data_state)) {

            esp_data_state pkt;
            memcpy(&pkt, data, sizeof(pkt));

            if (pkt.SETUP_UNIT_CK) {

                switch (pkt.UNIT_ERR) {

                    case UNIT_CK:
                        lcd.setCursor(0, 0);
                        lcd.print("SU CONNECTED    ");
                        break;

                    case ERR_WATCHDOG_RESET:
                        lcd.setCursor(0, 0);
                        lcd.print("RESTART SU NOW!");
                        break;

                    case ERR_BROWNOUT:
                        lcd.setCursor(0, 0);
                        lcd.print("DROP.VOLT ERR SU");
                        break;

                    default:
                        lcd.setCursor(0, 0);
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

    // ---- send-confirm handler ----
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

    // ---- public send ----
    void send(bool start) {

        data_send_pkt.execute = start;
        last_command = start;
        esp_now_send(receiverMAC,
                     (const uint8_t*)&data_send_pkt,
                     sizeof(data_send_pkt));

    }

private:

    // Outgoing: command to Starting Unit
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

*/