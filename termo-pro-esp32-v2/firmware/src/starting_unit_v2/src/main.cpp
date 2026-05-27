#include <string>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_now.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "driver/gpio.h"
#include "driver/adc.h"
#include "esp_adc_cal.h"

static const char* TAG = "STARTING_UNIT";

// -------------------------------------------------------
// GPIO PIN DEFINITIONS  (ESP32-C3-SuperMini)
// -------------------------------------------------------
#define PIN_TEMP_SENSOR     GPIO_NUM_0      // ADC input — NTC/sensor before 10k resistor
#define PIN_BUTTON          GPIO_NUM_3      // 12mm LED button (+) via resistor
#define PIN_LED_GREEN       GPIO_NUM_5      // green LED via resistor  (ON state)
#define PIN_LED_RED         GPIO_NUM_6      // red LED via resistor    (OFF state)
#define PIN_TRANSISTOR      GPIO_NUM_7      // PN2222A base via resistor (relay drive)

// -------------------------------------------------------
// ESP-NOW — placeholder MAC of the master/controller unit
// Replace with actual MAC of your master ESP32
// -------------------------------------------------------
static uint8_t master_mac[ESP_NOW_ETH_ALEN] = { 0x14, 0x33, 0x5C, 0x2F, 0x9F, 0xA4 };

// -------------------------------------------------------
// ERROR CODES  (kept identical to original)
// -------------------------------------------------------
typedef enum : uint8_t {

    MA_OK               = 0,

    // COMMUNICATION
    ERR_LEN_INVALID     = 10,
    ERR_DATA_INVALID    = 11,

    // SYSTEM
    ERR_RST_0           = 23,
    ERR_BROWNOUT        = 30,
    ERR_REBOOTED        = 31,
    ERR_WATCHDOG_RESET  = 32,

} ma_error_t;

// -------------------------------------------------------
// PACKED STRUCTS  (must match master exactly)
// -------------------------------------------------------
typedef struct __attribute__((packed)) {
    bool execute;
} esp_data_receive_t;

typedef struct __attribute__((packed)) {
    bool    SETUP_MA_OK;
    uint8_t MA_ERROR;
} esp_data_send_t;

// -------------------------------------------------------
// RUNTIME STATE
// -------------------------------------------------------
static volatile int     send_retry_counter  = 0;
static volatile uint8_t last_error_code     = MA_OK;

// Queue so the send-callback can trigger retry outside ISR context
static QueueHandle_t    send_retry_queue;

// -------------------------------------------------------
// GPIO HELPERS
// -------------------------------------------------------
static void output_pin_init(gpio_num_t pin)
{
    gpio_config_t cfg = {};
    cfg.pin_bit_mask = (1ULL << pin);
    cfg.mode         = GPIO_MODE_OUTPUT;
    cfg.pull_up_en   = GPIO_PULLUP_DISABLE;
    cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
    cfg.intr_type    = GPIO_INTR_DISABLE;
    gpio_config(&cfg);
}

static void input_pin_init(gpio_num_t pin, gpio_pullup_t pu, gpio_pulldown_t pd)
{
    gpio_config_t cfg = {};
    cfg.pin_bit_mask = (1ULL << pin);
    cfg.mode         = GPIO_MODE_INPUT;
    cfg.pull_up_en   = pu;
    cfg.pull_down_en = pd;
    cfg.intr_type    = GPIO_INTR_DISABLE;
    gpio_config(&cfg);
}

// -------------------------------------------------------
// RELAY + LED CONTROL
// -------------------------------------------------------
static void relay_start(void)
{
    gpio_set_level(PIN_LED_GREEN, 1);
    gpio_set_level(PIN_LED_RED,   0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(PIN_TRANSISTOR, 1);
    ESP_LOGI(TAG, "relay ON");
}

static void relay_stop(void)
{
    gpio_set_level(PIN_LED_GREEN, 0);
    gpio_set_level(PIN_LED_RED,   1);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(PIN_TRANSISTOR, 0);
    ESP_LOGI(TAG, "relay OFF");
}

// Flash both LEDs N times — used for hard-fault signalling
static void led_flash(int times)
{
    for (int i = 0; i < times; i++) {
        gpio_set_level(PIN_LED_GREEN, 1);
        gpio_set_level(PIN_LED_RED,   1);
        vTaskDelay(pdMS_TO_TICKS(200));
        gpio_set_level(PIN_LED_GREEN, 0);
        gpio_set_level(PIN_LED_RED,   0);
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

// -------------------------------------------------------
// ESP-NOW SEND
// -------------------------------------------------------
static void espnow_send_status(uint8_t error)
{
    esp_data_send_t pkt;
    pkt.SETUP_MA_OK = true;
    pkt.MA_ERROR    = error;

    esp_err_t ret = esp_now_send(master_mac, (const uint8_t*)&pkt, sizeof(pkt));
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "esp_now_send failed: %s", esp_err_to_name(ret));
    }
}

// -------------------------------------------------------
// ERROR HANDLER  (called from receive callback / send callback)
// -------------------------------------------------------
static void handle_error(uint8_t error)
{
    last_error_code = error;

    if (send_retry_counter < 10) {
        send_retry_counter++;
        espnow_send_status(error);
    } else {
        send_retry_counter = 0;
        ESP_LOGW(TAG, "retry limit reached, dropping error 0x%02X", error);
    }
}

// -------------------------------------------------------
// ESP-NOW CALLBACKS
// NOTE: these fire in a high-priority WiFi task context —
//       keep them short, no heavy work inside.
// -------------------------------------------------------
static void on_data_recv(const esp_now_recv_info_t* recv_info,
                         const uint8_t*              data,
                         int                         len)
{
    if (len != sizeof(esp_data_receive_t)) {
        handle_error(ERR_LEN_INVALID);
        return;
    }

    esp_data_receive_t pkt;
    memcpy(&pkt, data, sizeof(pkt));

    if (pkt.execute) {
        relay_start();
    } else {
        relay_stop();
    }
}

static void on_data_sent(const uint8_t* mac, esp_now_send_status_t status)
{
    if (status == ESP_NOW_SEND_SUCCESS) {
        send_retry_counter = 0;
    } else {
        // Queue a retry signal; handle_error will re-send
        uint8_t err = last_error_code;
        xQueueSendFromISR(send_retry_queue, &err, NULL);
    }
}

// -------------------------------------------------------
// SEND RETRY TASK
// Processes queued retries from the send-callback
// -------------------------------------------------------
static void send_retry_task(void* arg)
{
    uint8_t err;
    while (true) {
        if (xQueueReceive(send_retry_queue, &err, portMAX_DELAY) == pdTRUE) {
            handle_error(err);
        }
    }
}

// -------------------------------------------------------
// BOOT REASON — maps IDF5 esp_reset_reason() to MA error codes
// -------------------------------------------------------
static uint8_t report_boot_status(void)
{
    esp_reset_reason_t r = esp_reset_reason();

    switch (r) {

        case ESP_RST_POWERON:
            return ERR_RST_0;

        case ESP_RST_TASK_WDT:
        case ESP_RST_INT_WDT:
        case ESP_RST_WDT:
            return ERR_WATCHDOG_RESET;

        case ESP_RST_BROWNOUT:
            return ERR_BROWNOUT;

        case ESP_RST_PANIC:
        case ESP_RST_SW:
        case ESP_RST_DEEPSLEEP:
        case ESP_RST_EXT:
        default:
            return ERR_REBOOTED;
    }
}

// -------------------------------------------------------
// STUB — TEMPERATURE SENSOR  (GPIO0 / ADC)
// GPIO0 = ADC1_CH0 on ESP32-C3
// Replace the body with your actual sensor read + send logic
// -------------------------------------------------------
static void temp_sensor_task(void* arg)
{
    // ADC oneshot or legacy driver — example uses legacy for simplicity
    adc1_config_width(ADC_WIDTH_BIT_12);
    adc1_config_channel_atten(ADC1_CHANNEL_0, ADC_ATTEN_DB_11); // 0–3.9 V range

    while (true) {
        int raw = adc1_get_raw(ADC1_CHANNEL_0);

        // TODO: convert raw to temperature (depends on your sensor / NTC curve)
        ESP_LOGD(TAG, "temp ADC raw: %d", raw);

        vTaskDelay(pdMS_TO_TICKS(5000)); // read every 5 s — adjust as needed
    }
}

// -------------------------------------------------------
// STUB — BUTTON  (GPIO3)
// Simple polling example; swap for gpio interrupt if preferred
// -------------------------------------------------------
static void button_task(void* arg)
{
    int last_state = 1; // assume pulled HIGH when idle

    while (true) {
        int state = gpio_get_level(PIN_BUTTON);

        if (state == 0 && last_state == 1) {
            // TODO: add your button-press action here
            ESP_LOGI(TAG, "button pressed");
        }

        last_state = state;
        vTaskDelay(pdMS_TO_TICKS(50)); // 50 ms debounce poll
    }
}

// -------------------------------------------------------
// WIFI + ESP-NOW INIT
// -------------------------------------------------------
static esp_err_t espnow_init(void)
{
    // NVS required by WiFi driver
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    wifi_init_config_t wifi_cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&wifi_cfg));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE));
    ESP_ERROR_CHECK(esp_wifi_start());

    // Disable power save so ESP-NOW latency is minimal
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));

    if (esp_now_init() != ESP_OK) {
        ESP_LOGE(TAG, "esp_now_init failed");
        return ESP_FAIL;
    }

    ESP_ERROR_CHECK(esp_now_register_recv_cb(on_data_recv));
    ESP_ERROR_CHECK(esp_now_register_send_cb(on_data_sent));

    // Register master as peer
    esp_now_peer_info_t peer = {};
    memcpy(peer.peer_addr, master_mac, ESP_NOW_ETH_ALEN);
    peer.channel  = 1;
    peer.ifidx    = WIFI_IF_STA;
    peer.encrypt  = false;

    if (esp_now_add_peer(&peer) != ESP_OK) {
        ESP_LOGE(TAG, "esp_now_add_peer failed");
        return ESP_FAIL;
    }

    return ESP_OK;
}

// -------------------------------------------------------
// GPIO INIT
// -------------------------------------------------------
static void gpio_init_all(void)
{
    output_pin_init(PIN_LED_GREEN);
    output_pin_init(PIN_LED_RED);
    output_pin_init(PIN_TRANSISTOR);

    // Button: external resistor already on board, use internal pull-up as safety net
    input_pin_init(PIN_BUTTON, GPIO_PULLUP_ENABLE, GPIO_PULLDOWN_DISABLE);

    // Safe default state: relay off, red LED on
    gpio_set_level(PIN_TRANSISTOR, 0);
    gpio_set_level(PIN_LED_GREEN,  0);
    gpio_set_level(PIN_LED_RED,    1);
}

// -------------------------------------------------------
// APP MAIN
// -------------------------------------------------------
extern "C" void app_main(void)
{
    gpio_init_all();

    send_retry_queue = xQueueCreate(8, sizeof(uint8_t));

    if (espnow_init() != ESP_OK) {
        // Hard fault — flash 3 times then reboot (mirrors original behaviour)
        led_flash(3);
        esp_restart();
    }

    // Stabilise before first send
    vTaskDelay(pdMS_TO_TICKS(100));

    // Report why we booted
    uint8_t boot_err = report_boot_status();
    espnow_send_status(boot_err);
    ESP_LOGI(TAG, "boot status sent: 0x%02X", boot_err);

    // Spawn support tasks
    xTaskCreate(send_retry_task, "send_retry", 2048, NULL, 5, NULL);
    xTaskCreate(temp_sensor_task, "temp_sensor", 2048, NULL, 3, NULL);
    xTaskCreate(button_task,      "button",      2048, NULL, 3, NULL);

    // Main task has nothing left to do — delete itself
    vTaskDelete(NULL);
}
