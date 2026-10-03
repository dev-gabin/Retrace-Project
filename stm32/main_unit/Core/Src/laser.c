#include "laser.h"
#include "main.h"

/* PA8 LASER_EN: HIGH = 켜짐 (부팅 시 CubeMX가 LOW로 시작) */

void laser_on(void)
{
  HAL_GPIO_WritePin(LASER_EN_GPIO_Port, LASER_EN_Pin, GPIO_PIN_SET);
}

void laser_off(void)
{
  HAL_GPIO_WritePin(LASER_EN_GPIO_Port, LASER_EN_Pin, GPIO_PIN_RESET);
}
