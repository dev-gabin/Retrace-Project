#include "drawer.h"

/* Temporary values: calibrate with the assembled mechanism before use. */
#define SERVO_RETURN_PULSE_US 1000U
#define SERVO_PUSH_PULSE_US   2000U
#define SERVO_PUSH_MS         500U
#define SERVO_RETURN_MS       500U
#define LED_TIMEOUT_MS        10000U

static TIM_HandleTypeDef *timers[2];
static const uint32_t channels[3] = { TIM_CHANNEL_1, TIM_CHANNEL_2, TIM_CHANNEL_3 };
static GPIO_TypeDef *const led_ports[6] = {
  LED_1_GPIO_Port, LED_2_GPIO_Port, LED_3_GPIO_Port,
  LED_4_GPIO_Port, LED_5_GPIO_Port, LED_6_GPIO_Port
};
static const uint16_t led_pins[6] = {
  LED_1_Pin, LED_2_Pin, LED_3_Pin, LED_4_Pin, LED_5_Pin, LED_6_Pin
};
typedef enum { SERVO_IDLE, SERVO_PUSH, SERVO_RETURN } ServoPhase;
static ServoPhase phase;
static int active, pending, lit;
static uint32_t servo_since, led_since;

static void pulse(int n, uint32_t us)
{
  __HAL_TIM_SET_COMPARE(timers[(n - 1) / 3], channels[(n - 1) % 3], us);
}

void drawer_led_all_off(void)
{
  for (int i = 0; i < 6; ++i) HAL_GPIO_WritePin(led_ports[i], led_pins[i], GPIO_PIN_RESET);
  lit = 0;
}

void drawer_led(int n, int on)
{
  if (n < 1 || n > 6) return;
  if (on) {
    drawer_led_all_off();
    HAL_GPIO_WritePin(led_ports[n - 1], led_pins[n - 1], GPIO_PIN_SET);
    lit = n;
    led_since = HAL_GetTick();
  } else {
    HAL_GPIO_WritePin(led_ports[n - 1], led_pins[n - 1], GPIO_PIN_RESET);
    if (lit == n) lit = 0;
  }
}

void drawer_init(TIM_HandleTypeDef *tim3, TIM_HandleTypeDef *tim4)
{
  timers[0] = tim3;
  timers[1] = tim4;
  phase = SERVO_IDLE;
  active = pending = 0;
  drawer_led_all_off();
  for (int n = 1; n <= 6; ++n) {
    pulse(n, 0); /* Keep startup stationary until calibrated. */
    if (HAL_TIM_PWM_Start(timers[(n - 1) / 3], channels[(n - 1) % 3]) != HAL_OK)
      Error_Handler();
  }
}

void drawer_open(int n)
{
  if (n < 1 || n > 6) return;
  drawer_led(n, 1);
  pending = n;
  if (phase == SERVO_PUSH) {
    pulse(active, SERVO_RETURN_PULSE_US);
    phase = SERVO_RETURN;
    servo_since = HAL_GetTick();
  }
}

void drawer_poll(void)
{
  uint32_t now = HAL_GetTick();
  if (lit && (uint32_t)(now - led_since) >= LED_TIMEOUT_MS) drawer_led_all_off();
  if (phase == SERVO_PUSH && (uint32_t)(now - servo_since) >= SERVO_PUSH_MS) {
    pulse(active, SERVO_RETURN_PULSE_US);
    phase = SERVO_RETURN;
    servo_since = now;
  } else if (phase == SERVO_RETURN && (uint32_t)(now - servo_since) >= SERVO_RETURN_MS) {
    pulse(active, 0);
    active = 0;
    phase = SERVO_IDLE;
  }
  if (phase == SERVO_IDLE && pending) {
    active = pending;
    pending = 0;
    pulse(active, SERVO_PUSH_PULSE_US);
    servo_since = now;
    phase = SERVO_PUSH;
  }
}
