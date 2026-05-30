
/*
 * starting-module-c3.cpp
 * ESP32-C3-SuperMini  —  ESP-IDF >= 5.x
 *
 * GPIO MAP
 * --------
 * GPIO0  → NTC thermistor (ADC input only, 10 k pull-down on board)
 * GPIO1  → unconnected
 * GPIO2  → unconnected
 * GPIO3  → resistor → 12 mm LED-button (+)  [auxiliary, output only here]
 * GPIO5  → resistor → green LED
 * GPIO6  → resistor → red   LED
 * GPIO7  → resistor → PN2222A base  (motor/load switch)
 * 3.3 V  → NTC sensor VCC
 * 5 V    → board VIN
 */
#include <atomic>
#include <string>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_now.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "driver/gpio.h"

// ### TAG ####################################################################
static const char* TAG = "LOG[SU]";

// ### PIN DEFINITIONS ########################################################
static constexpr gpio_num_t PIN_TEMP_SENSE = GPIO_NUM_0;   // ADC input  (NTC);
static constexpr gpio_num_t M_PIN_BTN_LED  = GPIO_NUM_3;   // manual_start_button LED control pin;
static constexpr gpio_num_t M_PIN_BTN      = GPIO_NUM_4;   // manual_start_button logic input;
static constexpr gpio_num_t PIN_LED_GREEN  = GPIO_NUM_5;   // ON LED;
static constexpr gpio_num_t PIN_LED_RED    = GPIO_NUM_6;   // OFF LED;
static constexpr gpio_num_t PIN_TRANSISTOR = GPIO_NUM_7;   // PN2222A base -> RELAY;

// ### ESP-NOW PEER  (replace with real MAC before flashing!) ##################
static uint8_t receiver_mac[6] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };

// ### ERROR CODES ############################################################
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

// ### PAYLOAD STRUCTS ########################################################
struct [[gnu::packed]] esp_data_receive {
    bool execute;
};

struct [[gnu::packed]] esp_data_state {
    bool    SETUP_UNIT_CK;
    uint8_t UNIT_ERR;
};

struct [[gnu::packed]] esp_data_sensor { // IMPLEMENT LOGIC ON THE THERMOSTAT UNIT SIDE AND ALSO EXT_SENSOR LOGIC HERE;
    uint8_t SENSOR_CK;
    float   EXT_TEMP_VAL;
};

// ### MODULE STATE ############################################################
static std::atomic<bool> is_on      = false;
static int           retry_counter  = 0;
static uint8_t       last_error     = UNIT_CK;
static esp_data_state data_sent      = {};


// DELAY
void delay(int d) {
    vTaskDelay(pdMS_TO_TICKS(d));
}

// ### GPIO HELPERS ############################################################
std::atomic<bool> manual_ovveride = false;

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
static void espnow_send(uint8_t error)
{

    /*
    
        MODIFIED FOR 2 different packet types
    
    */
    data_sent.SETUP_UNIT_CK = true;
    data_sent.UNIT_ERR      = error;

    esp_err_t ret = esp_now_send(receiver_mac, (const uint8_t*)&data_sent, sizeof(data_sent));
    if (ret != ESP_OK) {

        ESP_LOGE(TAG, "esp_now_send failed: %s", esp_err_to_name(ret));

    }

}

// ### ERROR HANDLER ###########################################################
static void handle_error(uint8_t error)
{

    last_error = error;

    if (retry_counter < 10) {

        retry_counter++;
        espnow_send(error);

    } else {

        ESP_LOGE(TAG, "Retry limit hit - restarting");
        esp_restart();

    }

}

// ### ESP-NOW CALLBACKS #######################################################

// IDF 5.x receive callback signature
static void on_receive(const esp_now_recv_info_t* recv_info, const uint8_t*data, int len)
{
    (void)recv_info;

    if (len != (int)sizeof(esp_data_receive)) {

        handle_error(ERR_LEN_INVALID);
        return;

    }

    esp_data_receive pkt;
    memcpy(&pkt, data, sizeof(pkt));

    if (pkt.execute) {

        start();

    } else {

        stop();

    }
}

static void on_sent(const esp_now_send_info_t *tx_info, esp_now_send_status_t status)
{

    if (status == ESP_NOW_SEND_SUCCESS) {

        retry_counter = 0;

    } else {

        handle_error(last_error);

    }

}

// ### BOOT STATUS #############################################################
static uint8_t report_boot_status()
{
    switch (esp_reset_reason()) 
    {
        case ESP_RST_BROWNOUT:
            return ERR_BROWNOUT;

        case ESP_RST_WDT:
        case ESP_RST_TASK_WDT:
            return ERR_WATCHDOG_RESET;

        case ESP_RST_POWERON:
            return UNIT_CK;

        default:                
            return ERR_REBOOTED;
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

        ESP_LOGE(TAG, "UNIT_START failed — restarting");
        esp_restart();

    }

    ESP_ERROR_CHECK(esp_now_register_recv_cb(on_receive));
    ESP_ERROR_CHECK(esp_now_register_send_cb(on_sent));

    esp_now_peer_info_t peer = {};
    memcpy(peer.peer_addr, receiver_mac, 6);
    peer.channel = 0;
    peer.encrypt = false;
    ESP_ERROR_CHECK(esp_now_add_peer(&peer));

}

void manual_start(void *p) {

    while (true)
    {

        if(gpio_get_level(M_PIN_BTN) == 0) {

            if(!manual_ovveride) {

                manual_ovveride = true;
                aux_button_led_on();
                
                if(!is_on.load())
                    set_load(true, 1);

            } else {
                
                manual_ovveride = false;
                aux_button_led_off();

                if(!is_on.load())
                    set_load(false, 1);

            }

        }

        delay(100);

    }
    
}

/*

    LOGIC FOR EXTERNAL TEMPERATURE SENSOR TO BE IMPLEMENTED!
    - Pin init&atten&cali;
    - Read&Interpret value of the thermistor B3950 10k;
    - Send the temperature value to Thermostat Unit;
    - Thats al;

    void external_sensor(void *p) 
    {}

*/

// ### APP MAIN ################################################################
extern "C" void app_main()
{

    gpio_init_all();
    aux_button_led_off();       // latch auxiliary LED-button off;

    // wifi
    wifi_espnow_init();

    uint8_t boot_err = report_boot_status();
    if (boot_err != UNIT_CK) {

        ESP_LOGW(TAG, "Abnormal boot: error 0x%02X", boot_err);
        handle_error(boot_err);

    } else {

        // send clean-boot ok to controller-esp
        espnow_send(UNIT_CK);
        
    }

    delay(1000);
    xTaskCreatePinnedToCore(manual_start, "M_PIN_BTN",   4096, NULL, 15, NULL, 0); // importance 15;
    //     xTaskCreatePinnedToCore(external_sensor, "EXT_SENSOR",   8192, NULL, 10, NULL, 1); // importance 10;


}
