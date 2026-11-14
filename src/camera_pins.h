#ifndef CAMERA_PINS_H
#define CAMERA_PINS_H

// Standard AI-Thinker ESP32-CAM GPIO pin mapping for the OV2640 sensor.
// These values are the well-known AI-Thinker pinout used across the
// Espressif esp32-camera examples and the wider ESP32-CAM community, not
// invented values. Every one of these pins is reserved by the camera
// module and must never be reused for servos, buzzer, or LED wiring.
// See docs/PINOUT.md for the full reserved-vs-available GPIO table.

#define PWDN_GPIO_NUM     32
#define RESET_GPIO_NUM    -1  // Not connected on the AI-Thinker board
#define XCLK_GPIO_NUM      0
#define SIOD_GPIO_NUM     26
#define SIOC_GPIO_NUM     27

#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM        5

#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22

#endif // CAMERA_PINS_H
