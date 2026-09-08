#ifndef LOCAL_ALERT_H
#define LOCAL_ALERT_H

// Sets the buzzer and LED pins to OUTPUT. Call once from setup().
void alertInit();

// Turns the LED on and pulses the buzzer for a fixed duration. This is a
// delay()-based implementation, it briefly blocks the loop, acceptable at
// this MVP scale since it only fires on a real confirmed detection, not on
// every frame.
void triggerLocalAlert();

// Turns the LED and buzzer off. Called at the top of every response
// handling pass before the new frame's alert state is applied, so a
// false/null response clears a previous alert instead of latching it.
void clearLocalAlert();

#endif // LOCAL_ALERT_H
