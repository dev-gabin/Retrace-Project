#ifndef PAN_TILT_H
#define PAN_TILT_H

#include "main.h"

/* HOME 위치 (protocol.md 2장: 서보 중앙 90,90) */
#define PAN_TILT_HOME_PAN   90
#define PAN_TILT_HOME_TILT  90

/* PWM 시작 + HOME 위치로 이동 */
void pan_tilt_init(TIM_HandleTypeDef *htim);

/* 각도(0~180도)로 이동 */
void pan_tilt_set(int pan, int tilt);

void pan_tilt_home(void);

#endif /* PAN_TILT_H */
