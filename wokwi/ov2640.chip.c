// SecurIoT - OV2640 camera placeholder for Wokwi.
// Wokwi has no camera part: this custom chip only represents the
// AI-Thinker ESP32-CAM's onboard OV2640 and its wiring (see docs/PINOUT.md).
// It does not generate images.
#include "wokwi-api.h"
#include <stdio.h>

static const char *kPins[] = {
  "PWDN", "XCLK", "SIOD", "SIOC", "VSYNC", "HREF", "PCLK",
  "D7", "D6", "D5", "D4", "D3", "D2", "D1", "D0"
};

void chip_init(void) {
  for (unsigned i = 0; i < sizeof(kPins) / sizeof(kPins[0]); i++) {
    pin_init(kPins[i], INPUT);
  }
  printf("OV2640 (placeholder visual de SecurIoT) listo\n");
}
