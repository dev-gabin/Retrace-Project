#include "pan_tilt.h"

/* SG90 펄스 폭 (µs). TIM3는 1카운트 = 1µs (pinmap.md 1-3)
 * 확인 필요: 실물로 0도 · 180도 위치를 측정해서 조정 (pinmap.md 2-6) */
#define SERVO_PULSE_MIN_US   1000   /* 0도 */
#define SERVO_PULSE_MAX_US   2000   /* 180도 */

#define ANGLE_MAX     180
#define PAN_CHANNEL   TIM_CHANNEL_1   /* PA6 SERVO_PAN */
#define TILT_CHANNEL  TIM_CHANNEL_2   /* PA7 SERVO_TILT */

static TIM_HandleTypeDef *servo_tim;

/* 각도 → 펄스 폭. 0도 = MIN, 180도 = MAX 사이를 비례로 나눔 */
static uint32_t angle_to_pulse(int angle)
{
  if (angle < 0) {
    angle = 0;
  }
  if (angle > ANGLE_MAX) {
    angle = ANGLE_MAX;
  }
  return SERVO_PULSE_MIN_US
         + (uint32_t)angle * (SERVO_PULSE_MAX_US - SERVO_PULSE_MIN_US) / ANGLE_MAX;
}

void pan_tilt_init(TIM_HandleTypeDef *htim)
{
  servo_tim = htim;
  pan_tilt_home();
  HAL_TIM_PWM_Start(servo_tim, PAN_CHANNEL);
  HAL_TIM_PWM_Start(servo_tim, TILT_CHANNEL);
}

void pan_tilt_set(int pan, int tilt)
{
  /* CCR(비교 값) = HIGH 유지 시간. 다음 20ms 주기부터 반영됨 */
  __HAL_TIM_SET_COMPARE(servo_tim, PAN_CHANNEL, angle_to_pulse(pan));
  __HAL_TIM_SET_COMPARE(servo_tim, TILT_CHANNEL, angle_to_pulse(tilt));
}

void pan_tilt_home(void)
{
  pan_tilt_set(PAN_TILT_HOME_PAN, PAN_TILT_HOME_TILT);
}
