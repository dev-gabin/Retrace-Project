#ifndef DRAWER_H
#define DRAWER_H
#include "main.h"
void drawer_init(TIM_HandleTypeDef *tim3, TIM_HandleTypeDef *tim4);
void drawer_led(int n, int on);
void drawer_led_all_off(void);
void drawer_open(int n);
void drawer_poll(void);
#endif
