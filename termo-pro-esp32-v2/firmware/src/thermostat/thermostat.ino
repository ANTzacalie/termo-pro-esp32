#include "termostat.h"

// retry & safety
int counter = 0;
int failed_receive = 0;
bool last_command = false;

// receiver MAC (FIRING MODULE)
// example mac address, you need to get the mac address of your esp!!!
uint8_t receiverMAC[] = { 0xF4, 0xCF, 0xA2, 0xDE, 0xE1, 0xA2 };

// time / day-night
int day_start = 5;
int night_start = 21;
int hour = 0;
int day = 0;
int minute = 0;

// auto mode
int record_flag = 0;
long long int recorded_time;  // final in minutes

void set_time(int hour, int minute, int weekday) {
  struct tm t = {};

  // Dummy fixed date (Monday)
  t.tm_year = 2024 - 1900;
  t.tm_mon = 0;
  t.tm_mday = 1 + (weekday - 1);  // aligns weekday

  t.tm_hour = hour;
  t.tm_min = minute;
  t.tm_sec = 0;  // starts at 0
  t.tm_isdst = -1;

  time_t epoch = mktime(&t);

  struct timeval tv;
  tv.tv_sec = epoch;
  tv.tv_usec = 0;

  settimeofday(&tv, nullptr);
}


void read_time(int &hour, int &minute, int &weekday) {
  time_t now = time(nullptr);
  struct tm ti;
  localtime_r(&now, &ti);

  hour = ti.tm_hour;
  minute = ti.tm_min;

  weekday = ti.tm_wday;
  if (weekday == 0) weekday = 7;  // convert Sunday from 0 → 7
}

// program state
int program_choice = 0;
bool manual_control = false;


enum brightness_control : uint16_t {

  LOW_BC = 1200,     // 50
  MEDIUM_BC = 2400,  // 120
  HIGH_BC = 3400,    // 200
  ULTRA_BC = 4025    // 254

};

// auto mode instance
auto_mode confort_mode;

// programs
prog_1 program1;
prog_2 program2;
prog_3 program3;
prog_4 program4;
basic program0;

// used by all programs
int check_for_start(float current_temp, float start_temp) {


  if (current_temp <= start_temp) {
    record_flag = 1;
    return 1;
  }

  return 0;
}

int check_for_stop(float current_temp, float stop_temp) {

  if (current_temp >= stop_temp) {
    record_flag = 2;
    return 1;
  }

  return 0;
}

// button minimum press time for input
long int btnPressTime = 0;

// ESP-NOW static instance
local *local::instance = nullptr;

unsigned long lastTempRead = 0;

// custom arrow up for the LCD1602A
byte arrowUp[8] = {

  0b00100,
  0b01110,
  0b11111,
  0b00100,
  0b00100,
  0b00100,
  0b00100,
  0b00100

};

void clearArrowAt(int col, int row) {

  lcd.setCursor(col, row);
  lcd.print(" ");
}

unsigned long int end_save = 0;
unsigned long lastMoveTime = 0;


////////////////////////////////////////////////////////### DE REFACUT PENTRU 4 BUTOANE ###////////////////////////////////////////////////////////
void change_program(int prog_num) {

  lcd.clear();
  float temp_start = 0.00;
  float temp_stop = 0.00;

  int cursor = 7;
  lcd.setCursor(0, 0);
  lcd.print("EDITING P[");
  lcd.print(prog_num);
  lcd.print("]");

  lcd.setCursor(0, 1);
  lcd.printf("S:%04.1f  E:%04.1f ", temp_start, temp_stop);
  // T:25.6 TE:24.5
  lcd.setCursor(cursor, 1);
  lcd.print("*");

  end_save = millis();
  lastMoveTime = millis();

  while (true) {

    int btn_pressed = sensor.checkButtonInput();

    if (btn_pressed == 2 && cursor < 15) {

      clearArrowAt(cursor, 1);
      cursor = 15;
      end_save = millis();

      lcd.setCursor(0, 1);
      lcd.printf("S:%04.1f  E:%04.1f ", temp_start, temp_stop);

      // T:25.6 TE:24.5
      lcd.setCursor(cursor, 1);
      lcd.print("*");


    } else if (btn_pressed == 2 && cursor >= 15) {

      clearArrowAt(cursor, 1);
      cursor = 7;
      end_save = millis();

      lcd.setCursor(0, 1);
      lcd.printf("S:%04.1f  E:%04.1f ", temp_start, temp_stop);

      // T:25.6  T:24.5
      lcd.setCursor(cursor, 1);
      lcd.print("*");
    }

    if (btn_pressed == 1) {

      if (millis() - lastMoveTime > 100) {  // 50ms repeat

        if (cursor == 7 && temp_start < 40) {
          temp_start += 0.1f;
        } else {
          if (temp_stop < 42)
            temp_stop += 0.1f;
        }

        if (temp_start >= 40)
          temp_start = 0;
        if (temp_stop >= 42)
          temp_stop = 0;

        // update LCD
        lcd.setCursor(0, 1);
        lcd.printf("S:%04.1f  E:%04.1f ", temp_start, temp_stop);

        lcd.setCursor(cursor, 1);
        lcd.write("*");  //

        lastMoveTime = millis();  // reset timer
      }

      end_save = millis();  // last interaction time
    }

    if (millis() - end_save > 10000) {

      switch (prog_num) {

        case 1:

          program1.START_TEMP = temp_start;
          program1.STOP_TEMP = temp_stop;
          break;

        case 2:

          program2.START_TEMP = temp_start;
          program2.STOP_TEMP = temp_stop;
          break;

        case 3:

          program3.START_TEMP = temp_start;
          program3.STOP_TEMP = temp_stop;
          break;

        case 4:

          program4.START_TEMP = temp_start;
          program4.STOP_TEMP = temp_stop;
          break;

        case 5:

          program0.START_TEMP = temp_start;
          program0.STOP_TEMP = temp_stop;
          break;

        case 6:

          confort_mode.START_TEMP = temp_start;
          confort_mode.END_TEMP = temp_stop;
          break;

        default:
          break;
      }

      program_choice = prog_num;
      break;
    }
  }
}
////////////////////////////////////////////////////////### DE REFACUT PENTRU 4 BUTOANE ###////////////////////////////////////////////////////////

int last_time_press = 0;
long int last_time_set = 0;

bool last_check = false;
long int last_time_check = 0;

const int BTN_DELAY = 120;  // debounce / repeat delay
long int lastBtnTime = 0;

////////////////////////////////////////////////////////### DE REFACUT PENTRU 4 BUTOANE ###////////////////////////////////////////////////////////
// to be able to distinguish individual presses more easy
int readButtonDebounced() {
  int btn = sensor.checkButtonInput();

  if (btn != 0 && millis() - lastBtnTime > BTN_DELAY) {
    lastBtnTime = millis();

    // waits for release
    while (sensor.checkButtonInput() != 0)
      ;
    return btn;
  }
  return 0;
}
////////////////////////////////////////////////////////### DE REFACUT PENTRU 4 BUTOANE ###////////////////////////////////////////////////////////

class data {
private:
  Preferences prefs;

public:

  // Call this in setup()
  void begin() {

    prefs.begin("programs", false);
    load();
    prefs.end();
  }

  // Call this whenever user changes a value
  void save() {

    prefs.begin("programs", false);

    prefs.putFloat("auto_start", confort_mode.START_TEMP);
    prefs.putFloat("auto_end", confort_mode.END_TEMP);

    prefs.putFloat("p1_start", program1.START_TEMP);
    prefs.putFloat("p1_stop", program1.STOP_TEMP);

    prefs.putFloat("p2_start", program2.START_TEMP);
    prefs.putFloat("p2_stop", program2.STOP_TEMP);

    prefs.putFloat("p3_start", program3.START_TEMP);
    prefs.putFloat("p3_stop", program3.STOP_TEMP);

    prefs.putFloat("p4_start", program4.START_TEMP);
    prefs.putFloat("p4_stop", program4.STOP_TEMP);

    prefs.putFloat("basic_start", program0.START_TEMP);
    prefs.putFloat("basic_stop", program0.STOP_TEMP);

    prefs.end();
  }

private:

  void load() {

    confort_mode.START_TEMP = prefs.getFloat("auto_start", confort_mode.START_TEMP);
    confort_mode.END_TEMP = prefs.getFloat("auto_end", confort_mode.END_TEMP);

    program1.START_TEMP = prefs.getFloat("p1_start", program1.START_TEMP);
    program1.STOP_TEMP = prefs.getFloat("p1_stop", program1.STOP_TEMP);

    program2.START_TEMP = prefs.getFloat("p2_start", program2.START_TEMP);
    program2.STOP_TEMP = prefs.getFloat("p2_stop", program2.STOP_TEMP);

    program3.START_TEMP = prefs.getFloat("p3_start", program3.START_TEMP);
    program3.STOP_TEMP = prefs.getFloat("p3_stop", program3.STOP_TEMP);

    program4.START_TEMP = prefs.getFloat("p4_start", program4.START_TEMP);
    program4.STOP_TEMP = prefs.getFloat("p4_stop", program4.STOP_TEMP);

    program0.START_TEMP = prefs.getFloat("basic_start", program0.START_TEMP);
    program0.STOP_TEMP = prefs.getFloat("basic_stop", program0.STOP_TEMP);
  }
};
data storage;

void setup() {

  // lcd init
  lcd.begin(16, 2);
  lcd.clear();

  static local command;
  local::instance = &command;

  // WIFI
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(true);
  delay(100);
  
  if (esp_now_init() != ESP_OK) {
    wifi_reboot_error();
    ESP.restart();
  }

  esp_now_register_recv_cb(local::onReceive);
  esp_now_register_send_cb(local::onSent);

  esp_now_peer_info_t peer = {};
  memcpy(peer.peer_addr, receiverMAC, 6);
  peer.channel = 0;
  peer.encrypt = false;
  esp_now_add_peer(&peer);
  // WIFI

  sensors_init();
  
  startup_screen();

}

void loop() {

  if (record_flag == 1 && confort_mode.calc_co_1 || confort_mode.calc_co_2) {

    recorded_time = millis();

  } else if (record_flag == 2) {

    recorded_time = millis() - recorded_time;
    recorded_time /= 1000;  // transform from ms to s
    recorded_time /= 60;    // from s to minutes;
    confort_mode.set_time_ntc(recorded_time);
    record_flag = 0;
  }

  // minimum 10 sec waiting time for reading the dht11 sensor
  if (millis() - lastTempRead >= 15000) {

    lastTempRead = millis();
    sensor.readTemperatureHumidity();
    sensor.readPhotoresistorValue();
  }

  // UI[A] , shows info about temperature, humidity, time, ON/OFF , DAY/NIGHT , current day and the program being used
  read_time(hour, minute, day);
  if (day > 7) {
    day = 1;
    set_time(hour, minute, day);
  }

  char timeStr[6];  // "HH:MM"
  // formats raw time;
  snprintf(timeStr, sizeof(timeStr), "%02d:%02d", hour, minute);

  char line[17];  // 16 chars + null terminator

  float temp = sensor.getTemperature();
  float hum = sensor.getHumidity();

  // Format the entire line
  snprintf(line, sizeof(line), "T:%04.1fC H:%02d%% P%d", temp, (int)hum, program_choice);

  // Prints to LCD
  lcd.setCursor(0, 0);
  lcd.print(line);

  lcd.setCursor(0, 1);
  // OO:MM DAY[N] D/N O/F
  lcd.print(timeStr);
  lcd.print(" ");
  lcd.print("DAY[");
  lcd.print(day);
  lcd.print("] ");

  if (hour > day_start && hour < night_start) {

    lcd.print("D");

  } else {

    lcd.print("N");
  }

  lcd.print(" ");

  if (last_command) {

    lcd.print("O");

  } else {

    lcd.print("F");
  }
  // UI[A]

  // every 7 days the day_increment resets to zero , and each day increments by 1
  if (confort_mode.day_increment < confort_mode.reset_adjustment) {

    if (confort_mode.day_increment < day) {

      confort_mode.day_increment++;
    }

  } else {

    confort_mode.calc_co_1 = true;
    confort_mode.calc_co_2 = true;
    confort_mode.adjust_start_temp_day = 0;
    confort_mode.adjust_start_temp_night = 0;
    confort_mode.day_increment = 0;
  }

  if (!last_check) {

    last_time_check = millis();
    last_check = true;
  }

  ////////////////////////////////////////////////////////### DE REFACUT PENTRU 4 BUTOANE ###////////////////////////////////////////////////////////
  if (!manual_control && millis() - last_time_check >= 20000 * 6) {  // test 5000 else 20000 * 6

    // logic to start/stop the fire module based on parameters
    switch (program_choice) {

      case program_auto:
        {

          if (confort_mode.calc_co_1 || confort_mode.calc_co_2) {

            int val = check_for_start(temp, confort_mode.START_TEMP);

            if (val == 1) {

              command_now.send(true);
            }

          } else if (confort_mode.adjust_start_temp_day != 0 || confort_mode.adjust_start_temp_night != 0) {

            if (hour > day_start && hour < night_start && confort_mode.adjust_start_temp_day != 0) {

              int val = check_for_start(temp, confort_mode.adjust_start_temp_day);

              if (val == 1) {

                command_now.send(true);
              }

            } else if (confort_mode.adjust_start_temp_night != 0) {

              int val = check_for_start(temp, confort_mode.adjust_start_temp_day);

              if (val == 1) {

                command_now.send(true);
              }
            }
          }

          int val = check_for_stop(temp, confort_mode.END_TEMP);
          if (val == 1) {

            command_now.send(false);
          }

          break;
        }
      case program_basic:
        {

          int val = check_for_start(temp, program0.START_TEMP);

          if (val == 1) {

            command_now.send(true);
          }

          val = check_for_stop(temp, program0.STOP_TEMP);

          if (val == 1) {

            command_now.send(false);
          }

          break;
        }
      case program_1:
        {

          int val = check_for_start(temp, program1.START_TEMP);

          if (val == 1) {

            command_now.send(true);
          }

          val = check_for_stop(temp, program1.STOP_TEMP);

          if (val == 1) {

            command_now.send(false);
          }


          break;
        }
      case program_2:
        {

          int val = check_for_start(temp, program2.START_TEMP);

          if (val == 1) {

            command_now.send(true);
          }


          val = check_for_stop(temp, program2.STOP_TEMP);

          if (val == 1) {

            command_now.send(false);
          }


          break;
        }
      case program_3:
        {

          int val = check_for_start(temp, program3.START_TEMP);

          if (val == 1) {

            command_now.send(true);
          }

          val = check_for_stop(temp, program3.STOP_TEMP);

          if (val == 1) {

            command_now.send(false);
          }


          break;
        }
      case program_4:
        {

          int val = check_for_start(temp, program4.START_TEMP);

          if (val == 1) {

            command_now.send(true);
          }

          val = check_for_stop(temp, program4.STOP_TEMP);

          if (val == 1) {

            command_now.send(false);
          }

          break;
        }
      default:
        {
          break;
        }
    }

    last_check = false;
  }

  // returns and saves the button(s) pressed
  int button_pressed = sensor.checkButtonInput();
  if (button_pressed != 0 && last_time_press != button_pressed) {

    btnPressTime = millis();
    last_time_press = button_pressed;
  }

  // manual restart;
  if (button_pressed == last_time_press && button_pressed == 3 && millis() - btnPressTime >= force_stop) {

    // forced restart;
    ESP.restart();
  }

  // activates manual_control and also starts/stops the fire module;
  if (button_pressed == last_time_press && button_pressed == 3 && millis() - btnPressTime >= 10000 && millis() - btnPressTime < 10200) {

    manual_control = true;
    command_now.send(true);
  }
  if (button_pressed == last_time_press && button_pressed == 2 && millis() - btnPressTime > 5000 && millis() - btnPressTime < 5200) {

    manual_control = true;
    command_now.send(false);
  }

  // ovverides manual control
  if (button_pressed == last_time_press && button_pressed == 2 && millis() - btnPressTime > 10000 && millis() - btnPressTime < 10200) {

    manual_control = false;
  }

  // all the meniu logic for selecting program, editing program and time and also about;
  if (button_pressed == last_time_press && button_pressed == 1 && millis() - btnPressTime >= 2000) {

    while (sensor.checkButtonInput() != 0)
      ;  // wait release
    lcd.clear();

    bool exitMenu = false;
    bool inSubMenu = false;

    int choice = 0;  // SPG = 1 EPG = 2 ETM = 3 EXI = 4
    int selector_0 = 1;
    int prev_selector_0 = selector_0;

    lcd.setCursor(0, 0);
    lcd.print("SPG EPG ETM EXIT");

    lcd.setCursor(selector_0, 1);
    lcd.write(byte(0));

    while (!exitMenu) {

      int btn = readButtonDebounced();
      if (btn == 2) {

        clearArrowAt(selector_0, 1);

        selector_0 += 4;
        choice++;

        if (selector_0 > 13) {

          selector_0 = 1;
          choice = 0;
        }

        lcd.setCursor(selector_0, 1);
        lcd.write(byte(0));

      } else if (btn == 1) {

        inSubMenu = true;
      }

      if (inSubMenu) {

        lcd.clear();

        switch (choice) {

          // ================= SPG =================
          case 0:
            {

              lcd.setCursor(0, 0);
              lcd.print("1  2  3  4  B  A");

              int selector = 0;
              int prev = 0;
              unsigned long start = millis();

              lcd.setCursor(selector, 1);
              lcd.write(byte(0));

              while (true) {

                int btn2 = readButtonDebounced();
                if (btn2 == 2) {

                  clearArrowAt(selector, 1);

                  selector += 3;
                  if (selector > 16) {

                    selector = 0;
                  }

                  lcd.setCursor(selector, 1);
                  lcd.write(byte(0));
                  start = millis();

                } else if (btn2 == 1) {

                  int sel = 0;
                  switch (selector) {

                    case 0:
                      sel = 1;
                      break;

                    case 3:
                      sel = 2;
                      break;

                    case 6:
                      sel = 3;
                      break;

                    case 9:
                      sel = 4;
                      break;

                    case 12:
                      sel = 5;
                      break;

                    case 15:
                      sel = 6;
                      break;

                    default:
                      sel = 0;
                      break;
                  }

                  program_choice = sel;
                  break;

                } else if (millis() - start > 10000) {

                  break;
                }
              }

              break;
            }

          // ================= EPG =================
          case 1:
            {

              lcd.setCursor(0, 0);
              lcd.print("1  2  3  4  B  A");

              int selector = 0;
              unsigned long start = millis();

              lcd.setCursor(selector, 1);
              lcd.write(byte(0));

              while (true) {

                int btn2 = readButtonDebounced();
                if (btn2 == 2) {

                  clearArrowAt(selector, 1);

                  selector += 3;
                  if (selector > 16) {

                    selector = 0;
                  }

                  lcd.setCursor(selector, 1);
                  lcd.write(byte(0));
                  start = millis();

                } else if (btn2 == 1) {

                  switch (selector) {

                    case 0:

                      change_program(1);
                      break;

                    case 3:

                      change_program(2);
                      break;

                    case 6:

                      change_program(3);
                      break;

                    case 9:

                      change_program(4);
                      break;

                    case 12:

                      change_program(5);
                      break;

                    case 15:

                      change_program(6);
                      break;

                    default:
                      break;
                  }

                  storage.save();
                  break;

                } else if (millis() - start > 10000) {

                  break;
                }
              }
              break;
            }

          // ================= ETM =================
          case 2:
            {

              int hour_c = 0, minutes_c = 0, day_c = 1;
              int selector = 0;
              unsigned long start = millis();

              while (true) {

                char timeStr[17];
                snprintf(timeStr, sizeof(timeStr), "H:%02d M:%02d D:[%d]", hour_c, minutes_c, day_c);

                lcd.setCursor(0, 0);
                lcd.print(timeStr);

                lcd.setCursor(selector, 1);
                lcd.write(byte(0));

                int btn2 = readButtonDebounced();
                if (btn2 == 2) {

                  clearArrowAt(selector, 1);

                  selector += 5;
                  if (selector > 12) {

                    selector = 0;
                  }

                  start = millis();

                } else if (btn2 == 1) {

                  if (selector == 0) {

                    if (hour_c < 23) {

                      hour_c++;

                    } else {

                      hour_c = 0;
                    }

                  } else if (selector == 5) {

                    if (minutes_c < 59) {

                      minutes_c++;

                    } else {

                      minutes_c = 0;
                    }
                  } else {

                    if (day_c < 7) {

                      day_c++;

                    } else {

                      day_c = 1;
                    }
                  }

                  start = millis();

                } else if (millis() - start > 10000) {

                  set_time(hour_c, minutes_c, day_c);
                  break;
                }
              }

              break;
            }

          // ================= EXIT =================
          case 3:
          default:

            exitMenu = true;
            break;
        }

        inSubMenu = false;
        lcd.clear();

        lcd.setCursor(0, 0);
        lcd.print("SPG EPG ETM EXIT");

        lcd.setCursor(selector_0, 1);
        lcd.write(byte(0));
      }
    }

    last_time_press = 0;
  }

  // resets last_time press
  if (button_pressed == 0) {
    last_time_press = 0;
  }

  ////////////////////////////////////////////////////////### DE REFACUT PENTRU 4 BUTOANE ###////////////////////////////////////////////////////////

  // dose the auto brightness for the LCD1602A no I2C
  // uses an NPN 2N2222A transistor to switch current, and brightness is controled using PWM, D = rez_val / 254 (254 duty cycles in total)
  if (millis() - last_time_set > 5000) {

    int ldr_read = sensor.getLight();
    if (ldr_read < LOW_BC) {

      sensor.setBrightness(50);

    } else if (ldr_read > LOW_BC && ldr_read < MEDIUM_BC) {

      sensor.setBrightness(130);

    } else if (ldr_read > MEDIUM_BC && ldr_read < HIGH_BC) {

      sensor.setBrightness(200);

    } else {

      sensor.setBrightness(254);
    }

    last_time_set = millis();
  }
}
