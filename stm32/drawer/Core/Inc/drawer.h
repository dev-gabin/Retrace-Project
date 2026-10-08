#ifndef DRAWER_H
#define DRAWER_H
#include "main.h"

typedef enum {
  DRAWER_EVENT_NONE,
  DRAWER_EVENT_COMPLETE,
  DRAWER_EVENT_I2C_ERROR
} DrawerEvent;

void drawer_init(I2C_HandleTypeDef *i2c);
void drawer_led(int n, int on);
void drawer_led_all_off(void);
int drawer_open(int n);
DrawerEvent drawer_poll(void);
int drawer_i2c_check(void);
#endif
