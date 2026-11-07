
/*
 * starting-module-c3.cpp
 * ESP32-C3-SuperMini  —  ESP-IDF >= 5.x and curve calibration
 *
 * GPIO MAP
 * --------
 * GPIO0  → NTC thermistor (ADC input only, 10 k pull-down on board)
 * GPIO1  → unconnected
 * GPIO2  → unconnected
 * GPIO3  → resistor → 12 mm LED-button (+)  [auxiliary, output only here]
 * GPIO5  → resistor → green LED
 * GPIO6  → resistor → red   LED
 * GPIO7  → resistor → PN2222A base  (relay switch)
 * 3.3 V  → NTC sensor VCC
 * 5 V    → board VIN
 */
#include <atomic>
#include <string.h>
#include <string>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_idf_version.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_now.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "driver/gpio.h"
#include <math.h>
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"

// ### TAG ####################################################################
static const char* TAG = "LOG[SU]";

// 16-byte ESP-NOW Local Master Key. MUST be identical on both boards.
static const uint8_t ESP_NOW_LMK[16] = { 0xDE, 0x24, 0x64, 0x98, 0x81, 0x22, 0x9E, 0xC9, 0xC9, 0xAD, 0x66, 0x90, 0x04, 0x64, 0xE0, 0x48 };

// ### PIN DEFINITIONS ########################################################
static constexpr gpio_num_t PIN_TEMP_SENSE = GPIO_NUM_0;   // ADC input  (NTC);
static constexpr gpio_num_t M_PIN_BTN_LED  = GPIO_NUM_3;   // manual_start_button LED control pin;
static constexpr gpio_num_t M_PIN_BTN      = GPIO_NUM_4;   // manual_start_button logic input;
static constexpr gpio_num_t PIN_LED_GREEN  = GPIO_NUM_5;   // ON LED;
static constexpr gpio_num_t PIN_LED_RED    = GPIO_NUM_6;   // OFF LED;
static constexpr gpio_num_t PIN_TRANSISTOR = GPIO_NUM_7;   // PN2222A base -> RELAY;
std::atomic<int> sensor_ck = 0;
std::atomic<float> sensor_temp = 0.0f;

/* ***- ESP-NOW PEER  (replace with real MAC before flashing!) -** */
static uint8_t receiver_mac[6] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };

// ### ERROR CODES ############################################################
typedef enum : uint8_t {

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

// ### PAYLOAD STRUCTS ########################################################
struct [[gnu::packed]] esp_data_receive {
    bool execute;
};

struct [[gnu::packed]] esp_data_sensor {
    uint8_t UNIT_ERR;
    uint8_t SENSOR_CK;
    float   EXT_TEMP_VAL;
    uint8_t RELAY_ON;
    uint8_t MANUAL_ON;
};

static_assert(sizeof(esp_data_receive) == 1, "Unexpected command packet size");
static_assert(sizeof(esp_data_sensor) == 8, "Unexpected status packet size");

// ### MODULE STATE ############################################################
static std::atomic<bool> is_on      = false;
static int           retry_counter  = 0;
static uint8_t       last_error     = UNIT_CK;
std::atomic<bool> manual_ovveride = false;

// Short Wi-Fi callbacks enqueue events; a normal FreeRTOS task handles GPIO / retries.
enum su_event_type : uint8_t {
    SU_COMMAND,
    SU_RX_INVALID,
    SU_TX_SUCCESS,
    SU_TX_FAILURE
};

struct su_event {
    su_event_type type;
    bool boiler_command;
};

static QueueHandle_t su_event_queue = nullptr;
static uint32_t error_retry_due_ms = 0;
static bool error_retry_scheduled = false;
static uint32_t last_thermostat_command_ms = 0;
static bool thermostat_has_command = false;


// DELAY
void delay(int d) {
    vTaskDelay(pdMS_TO_TICKS(d));
}

// ### GPIO HELPERS ############################################################
static inline void set_load(bool on, int8_t arg)
{

    if(!manual_ovveride.load() && !arg) {

        gpio_set_level(PIN_TRANSISTOR, on ? 1 : 0);
        gpio_set_level(PIN_LED_GREEN,  on ? 1 : 0);
        gpio_set_level(PIN_LED_RED,    on ? 0 : 1);
        ESP_LOGI(TAG, "AUTO_START");

    } else if(arg) {

        gpio_set_level(PIN_TRANSISTOR, on ? 1 : 0);
        gpio_set_level(PIN_LED_GREEN,  on ? 1 : 0);
        gpio_set_level(PIN_LED_RED,    on ? 0 : 1);
        ESP_LOGI(TAG, "MANUAL_START");

    }

}

// ### 12mm_LED ############################################################
static void aux_button_led_on()
{

    gpio_set_level(M_PIN_BTN_LED, 1);

}

static void aux_button_led_off()
{

    gpio_set_level(M_PIN_BTN_LED, 0);

}

// ### LOAD CONTROL ############################################################
static void start()
{
    is_on.store(true);
    set_load(true, 0);
    ESP_LOGI(TAG, "LED_G_ON");
}

static void stop()
{
    is_on.store(false);
    set_load(false, 0);
    ESP_LOGI(TAG, "LED_G_OFF");
}

// ### ESP-NOW SEND ############################################################
static void send(uint8_t error, uint8_t sensor_ck, float ext_temp)
{
    esp_data_sensor packet = {};
    packet.UNIT_ERR = error;
    packet.SENSOR_CK = sensor_ck;
    packet.EXT_TEMP_VAL = ext_temp;
    packet.RELAY_ON = gpio_get_level(PIN_TRANSISTOR) ? 1 : 0;
    packet.MANUAL_ON = manual_ovveride.load() ? 1 : 0;

    esp_err_t ret = esp_now_send(receiver_mac, (const uint8_t*)&packet, sizeof(packet));
    if (ret != ESP_OK) {

        ESP_LOGE(TAG, "esp_now_send failed: %s", esp_err_to_name(ret));

    }

}

// ### ERROR HANDLER ###########################################################
static void handle_error(uint8_t error)
{

    last_error = error;

    if (retry_counter < 5) {

        retry_counter++;
        error_retry_due_ms = static_cast<uint32_t>(esp_timer_get_time() / 1000ULL) + 400;
        error_retry_scheduled = true;

    } else {
        retry_counter = 0;
        error_retry_scheduled = false;
    }

}

// ### ESP-NOW CALLBACKS #######################################################
// Callback work is limited to queueing. The Wi-Fi task must never sleep.

// IDF 5.x receive callback signature
static void on_receive(const esp_now_recv_info_t* recv_info, const uint8_t* data, int len)
{

    if (!su_event_queue || !recv_info || !recv_info->src_addr ||
        memcmp(recv_info->src_addr, receiver_mac, sizeof(receiver_mac)) != 0) {
        return;
    }

    su_event event = {};
    event.type = SU_RX_INVALID;

    if (data && len == (int)sizeof(esp_data_receive)) {
        esp_data_receive pkt;
        memcpy(&pkt, data, sizeof(pkt));
        event.type = SU_COMMAND;
        event.boiler_command = pkt.execute;
    }

    (void)xQueueSend(su_event_queue, &event, 0);

}

static void on_sent(const esp_now_send_info_t* send_info, esp_now_send_status_t status)
{
    (void)send_info;

    if (!su_event_queue) {
        return;
    }

    su_event event = {};
    event.type = (status == ESP_NOW_SEND_SUCCESS) ? SU_TX_SUCCESS : SU_TX_FAILURE;
    (void)xQueueSend(su_event_queue, &event, 0);

}

static void link_worker(void *p)
{
    (void)p;
    su_event event = {};

    while (true) {

        if (xQueueReceive(su_event_queue, &event, pdMS_TO_TICKS(50)) == pdTRUE) {

            switch (event.type) {

                case SU_COMMAND:
                    last_thermostat_command_ms = static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);
                    thermostat_has_command = true;
                    if (event.boiler_command) {
                        start();
                    } else {
                        stop();
                    }

                    // Explicitly report ACTUAL relay and Starting Unit manual states.
                    send(UNIT_CK, sensor_ck, sensor_temp);
                    break;

                case SU_RX_INVALID:
                    handle_error(ERR_LEN_INVALID);
                    break;

                case SU_TX_SUCCESS:
                    retry_counter = 0;
                    break;

                case SU_TX_FAILURE:
                    handle_error(last_error);
                    break;

            }

        }

        uint32_t now = static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);

        // Safety timeout: loss of controller communication turns automatic heat OFF.
        // Local Starting Unit manual override still has priority.
        if (thermostat_has_command &&
            (uint32_t)(now - last_thermostat_command_ms) >= 35000) {
            thermostat_has_command = false;
            is_on.store(false);
            set_load(false, 0);
            send(UNIT_CK, sensor_ck, sensor_temp);
            ESP_LOGW(TAG, "Thermostat heartbeat lost: automatic relay OFF");
        }

        if (error_retry_scheduled &&
            (int32_t)(now - error_retry_due_ms) >= 0) {

            error_retry_scheduled = false;
            send(last_error, sensor_ck, sensor_temp);

        }

    }

}

// ### BOOT STATUS #############################################################
static uint8_t report_boot_status()
{
    switch (esp_reset_reason()) 
    {
        case ESP_RST_BROWNOUT:
            return 30;

        case ESP_RST_WDT:
        case ESP_RST_TASK_WDT:
            return 32;

        case ESP_RST_POWERON:
            return 1;

        default:                
            return 31;
    }
}

// ### GPIO INIT ###############################################################
static void gpio_init_all()
{

    // inputs: 12mm button
    gpio_set_direction(M_PIN_BTN, GPIO_MODE_INPUT);
    gpio_set_pull_mode(M_PIN_BTN, GPIO_PULLUP_ONLY);

    // outputs: transistor, LEDs, button-LED
    gpio_config_t out_cfg = {

        .pin_bit_mask = (1ULL << PIN_TRANSISTOR) |
                        (1ULL << PIN_LED_GREEN)  |
                        (1ULL << PIN_LED_RED)    |
                        (1ULL << M_PIN_BTN_LED),
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,

    };

    gpio_config(&out_cfg);

    // safe defaults
    gpio_set_level(PIN_TRANSISTOR, 0);
    gpio_set_level(PIN_LED_GREEN,  0);
    gpio_set_level(PIN_LED_RED,    1);   // RED on at rest (load off)

    // GPIO0 is input-only on ESP32-C3; no config call needed —
    // the pin floats as ADC1_CH0 / input; the external 10 k sets the bias.

}

// ### WIFI / ESP-NOW INIT #####################################################
static void wifi_espnow_init()
{

    // NVS required by Wi-Fi driver
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {

        nvs_flash_erase();
        nvs_flash_init();
        
    }

    // Minimal Wi-Fi init (ESP-NOW needs the radio)
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());

    // ESP-NOW
    if (esp_now_init() != ESP_OK) {

        ESP_LOGE(TAG, "ESP_NOW_WIFI failed — restarting");
        esp_restart();

    }

    ESP_ERROR_CHECK(esp_now_register_recv_cb(on_receive));
    ESP_ERROR_CHECK(esp_now_register_send_cb(on_sent));

    esp_now_peer_info_t peer = {};
    memcpy(peer.peer_addr, receiver_mac, 6);
    peer.channel = 0;
    peer.ifidx = WIFI_IF_STA;
    peer.encrypt = true; // AES128 encryption
    memcpy(peer.lmk, ESP_NOW_LMK, sizeof(peer.lmk));
    ESP_ERROR_CHECK(esp_now_add_peer(&peer));

}

void manual_start(void *p) {

    (void)p;
    bool previous_pressed = false;

    while (true)
    {

        bool pressed = gpio_get_level(M_PIN_BTN) == 0;

        // Edge detection prevents a long-held button from toggling repeatedly.
        if (pressed && !previous_pressed) { // better than using more delays.....

            if (!manual_ovveride.load()) {

                manual_ovveride.store(true);
                aux_button_led_on();
                set_load(true, 1);
                send(MANUAL_OVERRIDE, sensor_ck, sensor_temp);

            } else {

                manual_ovveride.store(false);
                aux_button_led_off();

                // Restore thermostat-requested relay state after manual override.
                set_load(is_on.load(), 1);
                send(UNIT_CK, sensor_ck, sensor_temp);

            }

        }

        previous_pressed = pressed;
        delay(50);

    }

}

// Applies the termistor ecuations and calibration;
/* T_EQ: T_C = 1 / (1/298,15 + 1/3950*ln(Rntc/10000)) - 273,15 */
void external_sensor(void *p)
{
    (void)p;

    constexpr float VCC_MV = 3300.0f; // 3.3V voltage supply to the termistor
    constexpr float R_FIXED = 10000.0f; // R2 from the divider
    constexpr float R_NTC_25 = 10000.0f; // 10k NTC
    constexpr float BETA = 3950.0f; // from the termistor caract.
    constexpr float T25_K = 298.15f; // 25 celsius in kelvin

    constexpr int SAMPLES = 32; // 32 samples
    constexpr adc_atten_t ATTEN = ADC_ATTEN_DB_12;
    const TickType_t period = pdMS_TO_TICKS(60000);

    // ADC CALBRATION //
    adc_unit_t unit;
    adc_channel_t channel;
    adc_oneshot_unit_handle_t adc = nullptr;
    adc_cali_handle_t calibration = nullptr;

    ESP_ERROR_CHECK(adc_oneshot_io_to_channel(
        static_cast<int>(PIN_TEMP_SENSE), &unit, &channel
    ));

    adc_oneshot_unit_init_cfg_t adc_cfg = {};
    adc_cfg.unit_id = unit;

    ESP_ERROR_CHECK(adc_oneshot_new_unit(&adc_cfg, &adc));

    adc_oneshot_chan_cfg_t channel_cfg = {};
    channel_cfg.atten = ATTEN;
    channel_cfg.bitwidth = ADC_BITWIDTH_12;

    ESP_ERROR_CHECK(adc_oneshot_config_channel(
        adc, channel, &channel_cfg
    ));

    adc_cali_curve_fitting_config_t calibration_cfg = {};
    calibration_cfg.unit_id = unit;
    calibration_cfg.atten = ATTEN;
    calibration_cfg.bitwidth = ADC_BITWIDTH_12;

    ESP_ERROR_CHECK(adc_cali_create_scheme_curve_fitting(
        &calibration_cfg, &calibration
    ));

    // ADC CALBRATION //

    TickType_t last_wake = xTaskGetTickCount();
    while (true) {
        float sum_mv = 0.0f;
        bool read_ok = true;
        bool saturated = false;

        sensor_temp = NAN;

        for (int i = 0; i < SAMPLES; ++i) { // dose the 32 samples
            int raw = 0;
            int mv = 0;

            if (adc_oneshot_read(adc, channel, &raw) != ESP_OK) {
                read_ok = false;
                break;
            }

            if (raw >= 4090) {
                saturated = true;
                break;
            }

            if (adc_cali_raw_to_voltage(calibration, raw, &mv) != ESP_OK) {
                read_ok = false;
                break;
            }

            sum_mv += static_cast<float>(mv);
            vTaskDelay(1);
        }

        sensor_ck = 1;

        if (!read_ok) {
            // Sensor configured, failed reading: sends NAN to termostat
            ESP_LOGW("EXT_SENSOR", "ADC read failed");

        } else if (saturated) {
            sensor_ck = 9;
            ESP_LOGW("EXT_SENSOR", "ADC saturated");

        } else {

            const float voltage_mv = sum_mv / SAMPLES;
            if (voltage_mv < 100.0f || voltage_mv > 2450.0f) {

                sensor_ck = 9;
                ESP_LOGW("EXT_SENSOR", "Voltage outside range: %.0f mV",voltage_mv);
                
            } else {
                const float resistance = R_FIXED * (VCC_MV / voltage_mv - 1.0f);

                const float inverse_kelvin = 1.0f / T25_K + logf(resistance / R_NTC_25) / BETA;

                if (isfinite(inverse_kelvin) && inverse_kelvin > 0.0f) {
                    sensor_temp = 1.0f / inverse_kelvin - 273.15f;
                }

                if (!isfinite(sensor_temp)) {
                    sensor_temp = NAN;
                    sensor_ck = 9;
                } else {
                    ESP_LOGI("EXT_SENSOR: ", "%.2f C", sensor_temp.load());
                }
            }
        }

        send(0, sensor_ck, sensor_temp);
        vTaskDelayUntil(&last_wake, period);
    }
}

// ### APP MAIN ################################################################
extern "C" void app_main()
{

    gpio_init_all();
    aux_button_led_off();       // latch auxiliary LED-button off;

    // wifi
    su_event_queue = xQueueCreate(16, sizeof(su_event));
    if (!su_event_queue) {
        ESP_LOGE(TAG, "ESP-NOW event queue creation failed");
        esp_restart();
    }
    wifi_espnow_init();
    xTaskCreatePinnedToCore(link_worker, "SU_LINK", 4096, NULL, 12, NULL, 0);

    uint8_t boot_err = report_boot_status();
    
    if (boot_err != 1) {

        ESP_LOGW(TAG, "Abnormal boot: error 0x%02X", boot_err);
        handle_error(boot_err);

    } else {

        // send clean-boot ok to controller-esp
        send(UNIT_CK, 0, 0);
        
    }
    
    delay(1000);
    xTaskCreatePinnedToCore(manual_start, "M_PIN_BTN", 8192, NULL, 15, NULL, 0); // importance 15;
    xTaskCreatePinnedToCore(external_sensor, "EXT_SENSOR",   4096, NULL, 10, NULL, 0); // importance 10;

}
