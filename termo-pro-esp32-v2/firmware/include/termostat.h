#pragma once
#include <Arduino.h>
#include <LiquidCrystal.h>
#include "DHT.h"
#include "termostat_variables.h"
#include <WiFi.h>
#include <time.h>
#include <sys/time.h>
#include <esp_now.h>
#include <Preferences.h>

// DHT11 setup
#define DHTPIN 23
#define DHTTYPE DHT11
DHT dht(DHTPIN, DHTTYPE);

// LCD pins: RS, E, D4, D5, D6, D7
LiquidCrystal lcd(18, 19, 4, 5, 16, 17);

// -------------------- Sensors --------------------
class sensors {
public:

    void setBrightness(int n) { 
        
        ledcWrite(pwmChannelDisplay, n); 
        
    }

    int checkButtonInput() {

        if (digitalRead(buttonApin) == LOW && digitalRead(buttonBpin) == LOW) return 3;
        if (digitalRead(buttonApin) == LOW) return 1;
        if (digitalRead(buttonBpin) == LOW) return 2;

        return 0;

    }

    void readTemperatureHumidity() {

        float h = dht.readHumidity();
        float t = dht.readTemperature();

        if (isnan(h) || isnan(t)) {

            lcd.setCursor(0, 0);
            lcd.print("DHT read error   ");
            return;

        }

        tempDHT11 = t;
        humidityDHT11 = h;

    }

    void readPhotoresistorValue() { 
        
        light = analogRead(ldrPin); 
        
    }

    float getTemperature() { return tempDHT11; }
    float getHumidity() { return humidityDHT11; }
    int getLight() { return light; }

private:
    float tempDHT11 = 0;
    float humidityDHT11 = 0;
    int light = 0;

} sensor;

// FIRING-MODULE ERROR_CODES
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

// -------------------- ESP-NOW communication --------------------
class local {
public:
    static local* instance;

    static void onReceive(const uint8_t* mac, const uint8_t* data, int len) {
        if (instance) instance->onReceiveFinal(mac, data, len);
    }
    static void onSent(const uint8_t* mac, esp_now_send_status_t status) {
        if (instance) instance->onSentFinal(mac, status);
    }

    void onReceiveFinal(const uint8_t* mac, const uint8_t* data, int len) {

        if (len != sizeof(data_received)) {

            if(failed_receive < 10) { 

                failed_receive++; 
                
            } 
            else { 

                send(false); 
                ESP.restart(); 
                
            }

            return;
        }

        memcpy(&data_received, data, sizeof(data_received));

        if(data_received.SETUP_MA_OK) {

            switch(data_received.MA_ERROR) {

                case 0: 
                    // do nothing, or show that the firing module is connected
                    break;
                case 23:
                    lcd.setCursor(0, 0);
                    lcd.print("UNKNOWN ERROR MA");
                    break;
                case 32:
                    // big problem , tells the user to manually restart the fireing module
                    lcd.setCursor(0, 0);
                    lcd.print("RESTART MA NOW!");
                    break;
                case 30:
                    // show an error on screen that the fireing module has voltage drops/power issues, may need replacement
                    lcd.setCursor(0, 0);
                    lcd.print("DROP.VOLT ERR MA");
                    break;
                default:
                    break;

            }

        }

    }

    void onSentFinal(const uint8_t* mac, esp_now_send_status_t status) {

        if(status == ESP_NOW_SEND_SUCCESS) {
            
             counter = 0;

        } else if(counter < 10) { 

            counter++; 
            send(last_command); 
            
        } else { 
            
            ESP.restart(); 

        }

    }

    void send(bool start) {

        data_sent.execute = start;
        last_command = start;
        esp_now_send(receiverMAC, (uint8_t*)&data_sent, sizeof(data_sent));

    }

private:

    struct [[gnu::packed]] esp_data_send { 
        
        bool execute; 
        
    } data_sent;

    struct [[gnu::packed]] esp_data_receive { 

        bool SETUP_MA_OK; 
        uint8_t MA_ERROR; 
        
    } data_received;

} command_now;
