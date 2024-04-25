//  Si5351 controller firmware.
//  Copyright (C) 2023 IvyTheIV
//
//  This file is part of Xverter.
//  Xverter is free software: you can redistribute it and/or modify
//  it under the terms of the GNU General Public License as published by
//  the Free Software Foundation, either version 3 of the License, or
//  (at your option) any later version.
//
//  Xverter is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details.
//
//  You should have received a copy of the GNU General Public License
//  along with this program.  If not, see <https://www.gnu.org/licenses/>.

// Uncomment to use SSD1306-based screens, otherwise SH1106 is assumed.
// #define SSD1306

#include <Adafruit_GFX.h>
#ifdef SSD1306
#include <Adafruit_SSD1306.h>
#else
#include <Adafruit_SH110X.h>
#endif
#include "Picopixel.h"
#include <Rotary.h>
#include <si5351.h>
#include <EEPROM.h>
#include "logo.h"

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define SCREEN_ADDRESS 0x3C

#define INPUT_A 3
#define INPUT_B 2
#define ENC_BUTTON 4

#define CORRECTION_MAX 99999L
#define DEBUG

#ifdef SSD1306
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
#else
Adafruit_SH1106G display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
#endif

Rotary encoder = Rotary(INPUT_A, INPUT_B);
Si5351 si5351;

bool encoder_btn_state = HIGH;
bool encoder_btn_state_prev = HIGH;

const size_t MAX_SAVED_VALUES = 11; // only 11. 
                                    // Otherwise arduino starts to misbehave.

volatile int32_t correction_val;
volatile int32_t shift_values[MAX_SAVED_VALUES];
size_t selected = 0;
char display_value[8];

uint32_t prev_millis = 0;
uint32_t current_millis;
const uint32_t SCREEN_OFF_INTERVAL = 60000;

void reset_screen_off_timer() {
    prev_millis = current_millis;
}

bool screen_turn_off_on_interval() {
    if (current_millis - prev_millis >= SCREEN_OFF_INTERVAL) {
        display.clearDisplay();
        display.display();
        return false;
    }
    return true;
}

volatile struct Menu {
    uint8_t cursor_index;
    int step_multiplier;
    bool update;
} menu;

void display_number(int x, int y, int32_t num) {
    display.setCursor(x, y);
    sprintf(display_value, "%06ld", (int32_t)num);

    if (num < 0)
        display.print("-");
    else
        display.print(" ");

    display.print(display_value[1]);
    display.print(display_value[2]);
    display.print(".");
    display.print(display_value[3]);
    display.print(display_value[4]);
    display.print(display_value[5]);
}

uint64_t calc_frequency(int32_t shift) {
    return (200000ULL - shift) * 100000ULL;
}

void enc_calibration(int dir) {
    correction_val += menu.step_multiplier * dir;
}

void enc_calibration_edit(int dir) {
    menu.cursor_index = (menu.cursor_index + 5 + dir) % 5;

    menu.step_multiplier = 1;
    for (int i = 0; i < (5 - 1 - menu.cursor_index); i++) {
        menu.step_multiplier *= 10;
    }
}

void enc_main_menu_scroll(int dir) {
    selected = (selected + MAX_SAVED_VALUES + dir) % MAX_SAVED_VALUES;
}

void enc_main_menu_edit_scroll(int dir) {
    shift_values[selected] += menu.step_multiplier * dir;
}

void enc_main_menu_edit_select(int dir) {
    menu.cursor_index = (menu.cursor_index + 6 + dir) % 6;

    menu.step_multiplier = menu.cursor_index;

    menu.step_multiplier = 1;
    for (int i = 0; i < (6 - 1 - menu.cursor_index); i++) {
        menu.step_multiplier *= 10;
    }
}

typedef void (*MenuFunc)(int);
MenuFunc menu_function = &enc_main_menu_scroll;

void calibration_loop() {
    noInterrupts();
    menu.step_multiplier = 1000;
    menu.cursor_index = 1;
    menu.update = true;

    menu_function = &enc_calibration;

    int32_t correction_val_copy = (int32_t)correction_val;
    interrupts();

    Menu menu_copy;

    bool exit = false;
    bool edit = false; // will turn true on the first iteration
    bool screen_is_on = true;
    si5351.set_freq(calc_frequency(53600), SI5351_CLK1);

    while(!exit) {
        noInterrupts();
        menu_copy.step_multiplier = menu.step_multiplier;
        menu_copy.cursor_index = menu.cursor_index;
        menu_copy.update = menu.update;
        menu.update = false;

        correction_val = constrain(
                correction_val, -CORRECTION_MAX, CORRECTION_MAX);
        correction_val_copy = correction_val;
        interrupts();

        current_millis = millis();
        if (screen_is_on) {
            screen_is_on = screen_turn_off_on_interval();
        }

        encoder_btn_state = digitalRead(ENC_BUTTON);
        if (encoder_btn_state != encoder_btn_state_prev && encoder_btn_state == LOW) {
            menu_copy.update = true;

            if (!edit) {
                edit = true;
                noInterrupts();
                menu_function = &enc_calibration_edit;
                interrupts();
            } else {
                edit = false;
                noInterrupts();
                menu_function = &enc_calibration;
                interrupts();

                if (menu_copy.cursor_index == 0) {
                    exit = true;
                    EEPROM.put(0, correction_val_copy);
                }
            }
        }

        if (menu_copy.update) {
            reset_screen_off_timer();
            screen_is_on = true;

            si5351.set_correction(correction_val_copy * 100, SI5351_PLL_INPUT_XO);

            display.clearDisplay();
            display.setCursor(30, 0);
            display.print(F("CALIBRATION"));
            display.drawFastHLine(0, 11, 127, 1);
            display.setCursor(1, 20);
            display.print(F("REF: "));
            display.print(F("200.000 +53.6MHz"));
            display.setCursor(1, 40);
            display.print(F("CAL:"));
            display.setCursor(101, 53);
            display.print(F("save"));

            display_number(36, 40, correction_val_copy);

            if (menu_copy.cursor_index == 0) {
                display.drawRect(99, 51, 4 * 6 + 3, 12, 1); // save box
            } else {

                if (menu_copy.cursor_index > 1)
                    menu_copy.cursor_index++;

                if (edit) {
                    display.drawRect((menu_copy.cursor_index + 7) * 6 - 2, 40 - 2, 9, 11, 1);
                }
                else {
                    display.drawFastHLine((menu_copy.cursor_index + 7) * 6, 48, 6, 1);
                }
            }
        }

        delay(50);
        encoder_btn_state_prev = encoder_btn_state;
        if (menu_copy.update)
            display.display();
    }
}

ISR(PCINT2_vect) {
    menu.update = true;

    unsigned char result = encoder.process();
    if (result == DIR_CW)
        menu_function(1);
    else if (result == DIR_CCW)
        menu_function(-1);
}

int32_t default_values[MAX_SAVED_VALUES] = {
    33600, 41000, 42500, 53600, 
    73100, 33600, 33600, 33600,
    33600, 33600, 33600, /* 33600, */
};

enum MainMenuModes {
    Scroll,
    EditScroll,
    EditSelect,
};

void main_menu_loop() {
    noInterrupts();
    menu.step_multiplier = 1000;
    menu.cursor_index = 2;
    menu.update = true;

    menu_function = &enc_main_menu_scroll;
    int32_t selected_shift_value = shift_values[selected];
    interrupts();

    Menu menu_copy;
    MainMenuModes mode = Scroll;
    bool screen_is_on = true;
    si5351.set_freq(calc_frequency(selected_shift_value), SI5351_CLK1);

    for(;;) {
        noInterrupts();
        menu_copy.step_multiplier = menu.step_multiplier;
        menu_copy.cursor_index = menu.cursor_index;
        menu_copy.update = menu.update;
        menu.update = false;

        shift_values[selected] = constrain(shift_values[selected], 20000, 80000);
        selected_shift_value = shift_values[selected];
        interrupts();

        current_millis = millis();
        if (screen_is_on) {
            screen_is_on = screen_turn_off_on_interval();
        }

        encoder_btn_state = digitalRead(ENC_BUTTON);
        if (encoder_btn_state != encoder_btn_state_prev && encoder_btn_state == LOW) {
            menu_copy.update = true;

            switch (mode) {
                case Scroll:
                    mode = EditSelect;
                    noInterrupts();
                    menu.cursor_index = menu_copy.cursor_index = 2;
                    menu.step_multiplier = menu_copy.step_multiplier = 1000;
                    menu_function = &enc_main_menu_edit_select;
                    interrupts();
                    break;
                case EditScroll:
                    mode = EditSelect;
                    noInterrupts();
                    menu_function = &enc_main_menu_edit_select;
                    interrupts();
                    break;
                case EditSelect:
                    noInterrupts();
                    if (menu_copy.cursor_index == 0) {
                        mode = Scroll;
                        menu_function = &enc_main_menu_scroll;
                        EEPROM.put(sizeof(int32_t) + sizeof(int32_t) * selected, selected_shift_value);
                    } else {
                        mode = EditScroll;
                        menu_function = &enc_main_menu_edit_scroll;
                    }
                    interrupts();
                    break;
            }
        }

        if (menu_copy.update) {
            reset_screen_off_timer();
            screen_is_on = true;

            display.clearDisplay();
            display.setCursor(6, 0);
            display.print(F("+ FREQUENCY SHIFT"));
            display.drawFastHLine(0, 11, 127, 1);
            display.drawFastHLine(53, 33, 37, 1);
            display.drawFastHLine(53, 45, 37, 1);
            display.drawBitmap(6, 18, logo_small, 32, 32, 1);

            display_number(48, 36, selected_shift_value);

#ifdef DEBUG
            display.setCursor(3, 54);
            display.print(200000L - selected_shift_value);
#endif

            if (menu_copy.cursor_index > 2)
                menu_copy.cursor_index++;

            switch (mode) {
                case Scroll:
                    si5351.set_freq(calc_frequency(selected_shift_value), SI5351_CLK1);

                    display.setFont(&Picopixel);
                    display_number(50, 36 - 16, shift_values[(selected + MAX_SAVED_VALUES - 2) % MAX_SAVED_VALUES]);
                    display_number(54, 36 - 8, shift_values[(selected + MAX_SAVED_VALUES - 1) % MAX_SAVED_VALUES]);
                    display_number(54, 40 + 12, shift_values[(selected + MAX_SAVED_VALUES + 1) % MAX_SAVED_VALUES]);
                    display_number(50, 40 + 20, shift_values[(selected + MAX_SAVED_VALUES + 2) % MAX_SAVED_VALUES]);
                    display.setFont(nullptr);
                    display.setTextSize(2);
                    // display.setCursor(112, 50);
                    if (selected + 1 > 9)
                        display.setCursor(100, 50);
                    else
                        display.setCursor(112, 50);
                    // display.setCursor(107, 43);
                    display.print(selected + 1);
                    display.setTextSize(1);
                    break;
                case EditScroll:
                    si5351.set_freq(calc_frequency(shift_values[selected]), SI5351_CLK1);
                    display.drawFastHLine((menu_copy.cursor_index + 8) * 6, 44, 6, 1);
                    display.setCursor(101, 53);
                    display.print(F("save"));
                    break;
                case EditSelect:
                    display.setCursor(101, 53);
                    display.print(F("save"));
                    if (menu_copy.cursor_index == 0) {
                        display.drawRect(99, 51, 4 * 6 + 3, 12, 1); // save box
                    } else {
                        display.drawRect((menu_copy.cursor_index + 8) * 6 - 2, 36 - 2, 9, 11, 1);
                    }
                    break;
            }

        }

        delay(50);
        encoder_btn_state_prev = encoder_btn_state;
        if (menu_copy.update)
            display.display();
    }
}

void setup() {
    pinMode(LED_BUILTIN, OUTPUT);
    pinMode(ENC_BUTTON, INPUT_PULLUP);

#ifdef SSD1306
    if (!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
#else
    if (!display.begin(SCREEN_ADDRESS, false)) {
#endif
        digitalWrite(LED_BUILTIN, HIGH);
        for(;;);
    }

    display.clearDisplay();
    display.drawBitmap(0, 0, logo, SCREEN_WIDTH, SCREEN_HEIGHT, 1);
    display.setTextSize(1);
    display.setTextColor(1);

    display.setFont(&Picopixel);
    display.setCursor(0, 63);
    display.print(F("github.com/IvyTheIV/Xverter"));

    display.setCursor(112, 63);
    display.print(F("v.1.6"));      // board version

    display.setFont(nullptr);
    display.display();

    noInterrupts();
    EEPROM.get(0, (int32_t&)correction_val);
    EEPROM.get(sizeof(int32_t), shift_values);

    for (uint8_t i = 0; i < MAX_SAVED_VALUES; i++) {
        if (shift_values[i] == -1) {
            shift_values[i] = default_values[i];
        }
    }
    int32_t correction = correction_val;
    interrupts();

    if (!si5351.init(SI5351_CRYSTAL_LOAD_8PF, SI5351_XTAL_FREQ, correction * 100)) {
        digitalWrite(LED_BUILTIN, HIGH);

        display.clearDisplay();
        display.setCursor(0,0);
        display.println(F("failed to initialize si5351"));
        display.println(F("check wiring"));
        display.display();
        for(;;);
    }

    si5351.set_freq(calc_frequency(shift_values[selected]), SI5351_CLK1);

    si5351.set_ms_source(SI5351_CLK1, SI5351_PLLB);
    si5351.set_freq_manual(20000000000ULL, 80000000000ULL, SI5351_CLK0);

    si5351.drive_strength(SI5351_CLK0, SI5351_DRIVE_8MA);
    si5351.drive_strength(SI5351_CLK1, SI5351_DRIVE_8MA);

    si5351.output_enable(SI5351_CLK0, 1);
    si5351.output_enable(SI5351_CLK1, 1);
    si5351.output_enable(SI5351_CLK2, 0);

    delay(3000);
    
    encoder.begin();

    // enable pin change interrupts
    PCICR |= (1 << PCIE2);
    PCMSK2 |= (1 << PCINT18) | (1 << PCINT19);
    
    if (!digitalRead(ENC_BUTTON)) {
        calibration_loop();
        display.clearDisplay();
        display.drawBitmap(0, 0, logo, SCREEN_WIDTH, SCREEN_HEIGHT, 1);
        display.display();
    }
    delay(500);

    main_menu_loop();
}

void loop() {};

