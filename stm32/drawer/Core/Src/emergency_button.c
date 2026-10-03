#include "emergency_button.h"
#include "main.h"
#define BUTTON_DEBOUNCE_MS 50U
static GPIO_PinState stable, candidate;
static uint32_t changed_at;

void emergency_button_init(void)
{
  stable = candidate = HAL_GPIO_ReadPin(SOS_BTN_GPIO_Port, SOS_BTN_Pin);
  changed_at = HAL_GetTick();
}

/* Sample both press and release: a held button must emit only one event. */
int emergency_button_poll(void)
{
  uint32_t now = HAL_GetTick();
  GPIO_PinState raw = HAL_GPIO_ReadPin(SOS_BTN_GPIO_Port, SOS_BTN_Pin);
  if (raw != candidate) { candidate = raw; changed_at = now; }
  if (stable != candidate && (uint32_t)(now - changed_at) >= BUTTON_DEBOUNCE_MS) {
    stable = candidate;
    return stable == GPIO_PIN_RESET;
  }
  return 0;
}
