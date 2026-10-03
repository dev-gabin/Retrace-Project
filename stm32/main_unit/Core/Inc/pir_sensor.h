#ifndef PIR_SENSOR_H
#define PIR_SENSOR_H

void pir_sensor_init(void);

/* HAL_GPIO_EXTI_Callback에서 PIR 핀일 때 호출 (인터럽트 안) */
void pir_sensor_on_exti(void);

/* 메인 루프에서 호출. 상태가 바뀌었으면 1을 돌려주고 *motion = 1(감지 시작) / 0(종료) */
int pir_sensor_poll(int *motion);

#endif /* PIR_SENSOR_H */
