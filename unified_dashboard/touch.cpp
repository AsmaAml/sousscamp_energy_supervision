#include <Arduino.h>
#include "touch.h"
#include <TAMC_GT911.h>

TAMC_GT911 ts = TAMC_GT911(
    TOUCH_GT911_SDA,
    TOUCH_GT911_SCL,
    TOUCH_GT911_INT,
    TOUCH_GT911_RST,
    max(TOUCH_MAP_X1, TOUCH_MAP_X2),
    max(TOUCH_MAP_Y1, TOUCH_MAP_Y2)
);

void touch_init(uint8_t addr)
{
    Wire.begin(TOUCH_GT911_SDA, TOUCH_GT911_SCL);
    ts.begin(addr);
    ts.setRotation(TOUCH_GT911_ROTATION);
    Serial.println("Touch GT911 initialisé");
}
