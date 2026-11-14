#ifndef RESET_BUTTON_H
#define RESET_BUTTON_H

#include <stdint.h>

// Reset and pairing button on GPIO12 (to GND, INPUT_PULLUP, active low).
// A short press does nothing but log. Holding it for 3 s erases the saved
// config (NVS namespace "securiot" plus the portal library settings) and
// restarts into the setup portal. Holding it while the board powers on has the
// same effect. See docs/PINOUT.md for why GPIO12 is safe for this.

// Configures the pin and seeds the state machine. Call once at the start of
// setup(). Does not touch the LED, so it can run before alertInit().
void resetButtonInit();

// Non-blocking poll: reads the pin, drives the LED overlay and runs the long
// press action. Cheap, call it every loop() iteration and inside every
// blocking wait. On a long press it never returns (erase, wait, restart).
void resetButtonPoll();

// delay() replacement that keeps polling the button in 10 ms slices.
void resetButtonDelay(uint32_t ms);

#endif // RESET_BUTTON_H
