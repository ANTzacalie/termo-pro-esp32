#include <ESP8266WiFi.h>
#include <espnow.h>
#include <iostream>

// 2 LEDs, optional, if required just add the pin numbers;
const uint8_t led_on = 5;
const uint8_t led_off = 4;

// pin  for in on the relay module
const uint8_t relayPin = 14;

int counter = 0;
uint8_t last_error_code;

// example mac address, you need to get the mac address of your esp!
uint8_t receiverMAC[] = { 0x14, 0x33, 0x5C, 0x2F, 0x9F, 0xA4 };

enum MA_Error : uint8_t {

  MA_OK = 0,

  // --------------------
  // COMMUNICATION
  // --------------------
  ERR_LEN_INVALID        = 10,
  ERR_DATA_INVALID       = 11,

  // --------------------
  // SYSTEM
  // --------------------
  ERR_RST_0              = 23,
  ERR_BROWNOUT           = 30,
  ERR_REBOOTED           = 31,
  ERR_WATCHDOG_RESET     = 32,

};

class local {

public:

    static local* instance;

    struct __attribute__((packed)) esp_data_receive { 

        bool execute; 

    } data_received;

    struct __attribute__((packed)) esp_data_send { 

        bool SETUP_MA_OK; 
        uint8_t MA_ERROR; 

    } data_sent;

    static void onReceive(uint8_t* mac, uint8_t* data, uint8_t len) { 
        if(instance) instance->onReceiveFinal(mac,data,len); 
    }

    static void onSent(uint8_t* mac, uint8_t status) { 
        if(instance) instance->onSentFinal(mac,status); 
    }

    void onReceiveFinal(uint8_t* mac,uint8_t* data,uint8_t len) {

        if(len!=sizeof(data_received)) { 

            handleError(ERR_LEN_INVALID); 
            return; 
            
        }

        memcpy(&data_received,data,sizeof(data_received));

        if(data_received.execute) { 

            start(); 

        } else { 

            stop(); 

        }
    }

    void onSentFinal(uint8_t* mac, uint8_t status) {

        if(status==0) counter=0; // 0 = success on ESP8266
        else handleError(last_error_code);
    }

    void handleError(uint8_t error) {

        last_error_code=error;

        if(counter < 10) { 

            counter++; 
            send(error);

        } else {

            counter = 0;

        }

    }

    void send(uint8_t error) { 

        data_sent.SETUP_MA_OK=true; 
        data_sent.MA_ERROR=error; 
        esp_now_send(receiverMAC, (uint8_t*)&data_sent, sizeof(data_sent));

    }

    void start() { 

        digitalWrite(led_on, HIGH);
        digitalWrite(led_off, LOW);

        delay(10);

        digitalWrite(relayPin, HIGH); 

    }

    void stop() { 

        digitalWrite(led_on, LOW);
        digitalWrite(led_off, HIGH);

        delay(10);

        digitalWrite(relayPin, LOW); 

    }

} command_now;

extern "C" {
  #include "user_interface.h"
}

uint8_t report_boot_status() {

  uint8_t r = system_get_rst_info()->reason;
  uint8_t err = MA_OK;

  switch (r) {

    case REASON_DEFAULT_RST:
      err = ERR_RST_0;
      break;

    case REASON_WDT_RST:
      err = ERR_WATCHDOG_RESET;
      break;

    case REASON_EXCEPTION_RST:
      err = ERR_REBOOTED;
      break;

    case REASON_SOFT_RESTART:
      err = ERR_REBOOTED;
      break;

    case REASON_DEEP_SLEEP_AWAKE:
      err = ERR_REBOOTED;
      break;

    case REASON_EXT_SYS_RST:
      err = ERR_REBOOTED;
      break;

    default:
      err = ERR_REBOOTED;
      break;
  }

  return err;
}

local* local::instance = nullptr;

void setup() {

    pinMode(led_on, OUTPUT);
    pinMode(led_off, OUTPUT);

    // WIFI 
    static local command;
    local::instance=&command;
    
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    wifi_set_channel(1);

    if(esp_now_init() != 0) {

        // Flash LED 3 times, signals visual error
        digitalWrite(led_on, HIGH);
        digitalWrite(led_off, HIGH);
        delay(200);
        digitalWrite(led_on, LOW);
        digitalWrite(led_off, LOW);
        delay(10);
        digitalWrite(led_on, HIGH);
        digitalWrite(led_off, HIGH);
        delay(200);
        digitalWrite(led_on, LOW);
        digitalWrite(led_off, LOW);
        delay(10);
        digitalWrite(led_on, HIGH);
        digitalWrite(led_off, HIGH);
        delay(200);
        digitalWrite(led_on, LOW);
        digitalWrite(led_off, LOW);

        ESP.restart();
    }

    esp_now_set_self_role(ESP_NOW_ROLE_CONTROLLER);
    esp_now_register_recv_cb(local::onReceive);
    esp_now_register_send_cb(local::onSent);

    esp_now_add_peer(receiverMAC, ESP_NOW_ROLE_SLAVE, 1, NULL, 0);
    // WIFI
    
    // delay to stabilize
    delay(100);

    pinMode(relayPin, OUTPUT);
    digitalWrite(relayPin, LOW);
    digitalWrite(led_on, LOW);
    digitalWrite(led_off, HIGH);
    command.send(report_boot_status());

}

void loop() {}