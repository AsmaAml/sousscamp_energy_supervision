#pragma once
#include <lvgl.h>
#include <Wire.h>
#include <Arduino.h>

/* GT911 touch controller */
#define TOUCH_GT911
#define TOUCH_GT911_SCL 16
#define TOUCH_GT911_SDA 15
#define TOUCH_GT911_INT -1
#define TOUCH_GT911_RST -1
#define TOUCH_GT911_ROTATION ROTATION_NORMAL
#define TOUCH_MAP_X1 240
#define TOUCH_MAP_X2 0
#define TOUCH_MAP_Y1 320
#define TOUCH_MAP_Y2 0

void touch_init(uint8_t addr);