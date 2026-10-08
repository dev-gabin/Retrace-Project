#include "pir_sensor.h"
#include "main.h"

/* 핀이 바뀐 뒤 이 시간 동안 그대로면 진짜 변화로 판단 (잡음 무시) */
#define PIR_STABLE_MS  50

static volatile uint8_t edge_flag = 0;   /* 인터럽트에서 세움 → 메인 루프에서 처리 */
static uint8_t checking = 0;             /* 안정 시간을 기다리는 중인지 */
static uint32_t edge_tick = 0;
static uint8_t reported = 0;             /* 마지막으로 Jetson에 보낸 상태 */

void pir_sensor_init(void)
{
  /* 부팅 때 이미 HIGH일 수 있으므로 시작하자마자 한 번 확인 */
  checking = 1;
  edge_tick = HAL_GetTick();
}

void pir_sensor_on_exti(void)
{
  edge_flag = 1;
}

int pir_sensor_poll(int *motion)
{
  uint32_t now = HAL_GetTick();

  if (edge_flag) {
    edge_flag = 0;
    checking = 1;
    edge_tick = now;        /* 또 바뀌면 처음부터 다시 기다림 */
  }
  /* unsigned 뺄셈이라 HAL_GetTick()이 넘쳐서 0으로 돌아가도 정상 동작 */
  if (!checking || now - edge_tick < PIR_STABLE_MS) {
    return 0;
  }
  checking = 0;

  uint8_t level = (HAL_GPIO_ReadPin(PIR_IN_GPIO_Port, PIR_IN_Pin) == GPIO_PIN_SET);
  if (level == reported) {
    return 0;
  }
  reported = level;
  *motion = level;
  return 1;
}
