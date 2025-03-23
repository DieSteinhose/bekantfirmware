

#include "btn.h"
#include <pic.h>
#include <stdbool.h>       /* For true/false definition */
#include <stdint.h>        /* For uint8_t definition */

// A mapping to PORTB
// Determined by PCB traces
typedef union {
    PORTBbits_t PORTB;
    struct {
        unsigned DOWN : 1;
        unsigned UP : 1;
        unsigned : 6;
    };
} ButtonState_t;

#define PRESSED(b) (!b)
#define RELEASED(b) (b)
#define BUTTON_CHANGE(a, b) ((a.UP != b.UP) || (a.DOWN != b.DOWN))

typedef struct {
    uint8_t count;
    ButtonState_t last_state;
} Debounce_t;

// Button state is polled at Timer2 frequency: 4000 Hz so every 250 us.
// 250 us * 200 count = 50 ms
#define DEBOUNCE_THRESHOLD 200

/**
 * 
 * @param now_btn newest button state
 * @return Whether the button state has changed after debouncing
 */
bool btn_debounce(ButtonState_t now_btn) {
    static Debounce_t debouncer = {
        .count = 0,
        .last_state = 0,
    };
    
    if (BUTTON_CHANGE(debouncer.last_state, now_btn)) {
        debouncer.count = 0;
        debouncer.last_state = now_btn;
    } else {
        debouncer.count++;
    }
    
    if (debouncer.count >= DEBOUNCE_THRESHOLD) {
        debouncer.count = 0;
        return true;
    } else {
        return false;
    }
}

bool btn_debounce(ButtonState_t now_btn);

typedef struct {
    uint8_t save_hold;
    uint8_t double_click_timer;  // Timer for double-click detection
    bool waiting_for_second_up;  // Flag for detecting double-click up
    bool waiting_for_second_down; // Flag for detecting double-click down
    INPUT_t state;
} InputState_t;

// Debounced input comes in at frequency Timer2 / DEBOUNCE_THRESHOLD
// 4000 Hz / 200 = 20 Hz
//   0.05 sec * 60 = 3 sec to hold SAVE gesture
#define SAVE_HOLD_THRESHOLD 60
// Time window for double-click in 50ms increments (20 = 1 second)
#define DOUBLE_CLICK_WINDOW 10

INPUT_t btn_gesture(ButtonState_t btn) {
    static InputState_t input = {
        .save_hold = 0,
        .double_click_timer = 0,
        .waiting_for_second_up = false,
        .waiting_for_second_down = false,
        .state = INPUT_IDLE,
    };

    // Decrement double-click timer if active
    if (input.double_click_timer > 0) {
        input.double_click_timer--;
        
        // Reset waiting flags if timer expires
        if (input.double_click_timer == 0) {
            input.waiting_for_second_up = false;
            input.waiting_for_second_down = false;
        }
    }

    switch (input.state) {
        case INPUT_IDLE:
            if (PRESSED(btn.UP) && RELEASED(btn.DOWN)) {
                if (input.waiting_for_second_up) {
                    // Double-click up detected
                    input.waiting_for_second_up = false;
                    input.double_click_timer = 0;
                    input.state = INPUT_DOUBLE_UP;
                } else {
                    // First up press
                    input.state = INPUT_UP;
                }
            } else if (RELEASED(btn.UP) && PRESSED(btn.DOWN)) {
                if (input.waiting_for_second_down) {
                    // Double-click down detected
                    input.waiting_for_second_down = false;
                    input.double_click_timer = 0;
                    input.state = INPUT_DOUBLE_DOWN;
                } else {
                    // First down press
                    input.state = INPUT_DOWN;
                }
            } else if (PRESSED(btn.UP) && PRESSED(btn.DOWN)) {
                input.save_hold++;
                if (input.save_hold >= SAVE_HOLD_THRESHOLD) {
                    input.save_hold = 0;
                    input.state = INPUT_SAVE;
                } else {
                    input.state = INPUT_IDLE;
                }
            } else {
                input.state = INPUT_IDLE;
            }
            break;
            
        case INPUT_SAVE:
            if (PRESSED(btn.UP) && PRESSED(btn.DOWN)) {
                input.state = INPUT_SAVE;
            } else {
                input.state = INPUT_IDLE;
            }
            break;
            
        case INPUT_UP:
            if (PRESSED(btn.UP) && RELEASED(btn.DOWN)) {
                input.state = INPUT_UP;
            } else if (RELEASED(btn.UP) && PRESSED(btn.DOWN)) {
                input.state = INPUT_DOWN;
            } else if (RELEASED(btn.UP) && RELEASED(btn.DOWN)) {
                // Button released, start waiting for second click
                input.waiting_for_second_up = true;
                input.double_click_timer = DOUBLE_CLICK_WINDOW;
                input.state = INPUT_IDLE;
            } else if (PRESSED(btn.UP) && PRESSED(btn.DOWN)) {
                input.state = INPUT_IDLE;
            }
            break;
            
        case INPUT_DOWN:
            if (PRESSED(btn.UP) && RELEASED(btn.DOWN)) {
                input.state = INPUT_UP;
            } else if (RELEASED(btn.UP) && PRESSED(btn.DOWN)) {
                input.state = INPUT_DOWN;
            } else if (RELEASED(btn.UP) && RELEASED(btn.DOWN)) {
                // Button released, start waiting for second click
                input.waiting_for_second_down = true;
                input.double_click_timer = DOUBLE_CLICK_WINDOW;
                input.state = INPUT_IDLE;
            } else if (PRESSED(btn.UP) && PRESSED(btn.DOWN)) {
                input.state = INPUT_IDLE;
            }
            break;
            
        case INPUT_DOUBLE_UP:
            if (RELEASED(btn.UP) && RELEASED(btn.DOWN)) {
                input.state = INPUT_IDLE;
            } else {
                input.state = INPUT_DOUBLE_UP;
            }
            break;
            
        case INPUT_DOUBLE_DOWN:
            if (RELEASED(btn.UP) && RELEASED(btn.DOWN)) {
                input.state = INPUT_IDLE;
            } else {
                input.state = INPUT_DOUBLE_DOWN;
            }
            break;
    }
    
    return input.state;
}

void (*btn_report_gesture)(INPUT_t gesture);

void btn_timer() {
    static INPUT_t last_input = INPUT_IDLE;

    ButtonState_t button_state = (ButtonState_t)PORTBbits;

    if (btn_debounce(button_state)) {
        INPUT_t input = btn_gesture(button_state);

        if (input != last_input) {
            last_input = input;

            btn_report_gesture(input);
        }
    }
}

void btn_init(void) {
    // Timer2 clock input is Fosc/4 (instruction clock)
    // System Fosc: 16 Mhz
    // Instruction clock: Fosc / 4 = 4 Mhz
    // 4 Mhz / 100 period / 10 postscaler = 4000 Hz
    //   0.00025 sec
    //   250 usec
    T2CONbits.T2CKPS = 0b00; // Prescaler is 1
    PR2bits.PR2 = 100; // Timer2 period
    T2CONbits.T2OUTPS = 0b1001; // 1:10 Postscaler

    T2CONbits.TMR2ON = 1; // Timer is on
    PIE1bits.TMR2IE = 1; // Enable Timer2 interrupt
}