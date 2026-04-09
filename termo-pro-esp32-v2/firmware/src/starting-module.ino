#include <WiFi.h>
#include <esp_now.h>

// 2 LEDs, optional, if required just add the pin numbers;
const int ledA;
const int ledB;

// pin 17 for in on the relay module
const int relayPin = 17;
bool isOn = false;
int counter = 0;
uint8_t last_error_code;
// example mac address, you need to get the mac address of your esp32!!!
uint8_t receiverMAC[] = { 0x14, 0x33, 0x5C, 0x2F, 0x9F, 0xA4 };

enum MA_Error : uint8_t {

  MA_OK = 0,

  // --------------------
  // COMMUNICATION
  // --------------------
  ERR_LEN_INVALID        = 10,  // Wrong ESP-NOW payload length
  ERR_DATA_INVALID       = 11,  // Payload content invalid

  // --------------------
  // SYSTEM
  // --------------------
  ERR_BROWNOUT           = 30,  // Power drop detected
  ERR_REBOOTED           = 31,  // Unexpected reset occurred
  ERR_WATCHDOG_RESET     = 32,  // Watchdog reset , a lot of stuff with this one ;(

};

class local {

public:

    static local* instance;

    struct [[gnu::packed]] esp_data_receive { 
        
        bool execute; 
        
    } data_received;
    struct [[gnu::packed]] esp_data_send { 
        
        bool SETUP_MA_OK; 
        uint8_t MA_ERROR; 

    } data_sent;

    static void onReceive(const uint8_t* mac, const uint8_t* data, int len) { 
        if(instance) instance->onReceiveFinal(mac,data,len); 
    }
    static void onSent(const uint8_t* mac, esp_now_send_status_t status) { 
        if(instance) instance->onSentFinal(mac,status); 
    }

    void onReceiveFinal(const uint8_t* mac,const uint8_t* data,int len) {

        if(len!=sizeof(data_received)) { 

            handleError(ERR_LEN_INVALID); 
            return; 

        }

        memcpy(&data_received,data,sizeof(data_received));
        if(data_received.execute) { 

            isOn=true; 
            start(); 

        } else { 
            
            isOn=false; 
            stop(); 
        
        }

    }

    void onSentFinal(const uint8_t* mac, esp_now_send_status_t status) {

        if(status==ESP_NOW_SEND_SUCCESS) counter=0; 
        else handleError(last_error_code);

    }

    void handleError(uint8_t error) {

        last_error_code=error;

        if(counter<10) { 

            counter++; 
            send(error);

        } else { 
            
            ESP.restart(); 

        }

    }

    void send(uint8_t error) { 

        data_sent.SETUP_MA_OK=true; 
        data_sent.MA_ERROR=error; 
        esp_now_send(receiverMAC,(uint8_t*)&data_sent,sizeof(data_sent)); 

    }

    void start() { 
        
        digitalWrite(relayPin, HIGH); 
       // digitalWrite(ledA, HIGH);
       // digitalWrite(ledB, LOW);
        
    }

    void stop() { 
        
        digitalWrite(relayPin, LOW); 
       // digitalWrite(ledA, LOW);
       // digitalWrite(ledB, HIGH);
        
    }

} command_now;

uint8_t report_boot_status() {

  esp_reset_reason_t r = esp_reset_reason();
  uint8_t err = MA_OK;

  switch (r) {

    case ESP_RST_BROWNOUT:
      err = ERR_BROWNOUT;
      break;

    case ESP_RST_WDT:
    case ESP_RST_TASK_WDT:
      err = ERR_WATCHDOG_RESET;
      break;

    case ESP_RST_POWERON:
      err = 23;
      break;

    default:
      err = ERR_REBOOTED;
      break;

  }

  return err;
}

local* local::instance=nullptr;
void setup() {

    // WIFI
    static local command;
    local::instance=&command;
    
    WiFi.mode(WIFI_STA);
    if(esp_now_init()!=ESP_OK) {
        ESP.restart();
    }

    esp_now_register_recv_cb(local::onReceive);
    esp_now_register_send_cb(local::onSent);

    esp_now_peer_info_t peer = {};
    memcpy(peer.peer_addr, receiverMAC, 6);
    peer.channel=0; peer.encrypt=false;
    esp_now_add_peer(&peer);
    // WIFI
    
    // sets pin mode and default state
    pinMode(relayPin, OUTPUT);
    //     pinMode(ledA, OUTPUT);
    //     pinMode(ledB, OUTPUT);
    // digitalWrite(ledB, HIGH);
    digitalWrite(relayPin, LOW);

}

void loop() {}

