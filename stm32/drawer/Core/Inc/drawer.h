#ifndef DRAWER_H
#define DRAWER_H
#include "main.h"
void drawer_init(I2C_HandleTypeDef *i2c);
void drawer_led(int n, int on);
void drawer_led_all_off(void);
void drawer_open(int n);
void drawer_poll(void);
#endif
