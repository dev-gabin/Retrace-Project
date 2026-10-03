/* Host tests: real firmware modules with simulated HAL, no hardware claims. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cmd_parser.h"
#include "serial_cmd.h"
#include "drawer.h"
#include "emergency_button.h"

GPIO_TypeDef test_gpio_a = {0}, test_gpio_b = {1}, test_gpio_c = {2};
static uint32_t tick, pulses[6], mask;
static uint16_t leds;
static GPIO_PinState button = GPIO_PIN_SET;
static uint8_t *rx[2];
static char tx[2][128];
static int total, failures;
#define CHECK(x) do { ++total; if (!(x)) { ++failures; printf("FAIL line %d: %s\n", __LINE__, #x); } } while (0)
uint32_t HAL_GetTick(void) { return tick; }
GPIO_PinState HAL_GPIO_ReadPin(GPIO_TypeDef *p, uint16_t n) { (void)p; (void)n; return button; }
void HAL_GPIO_WritePin(GPIO_TypeDef *p, uint16_t n, GPIO_PinState s) {
  (void)p; if (s) leds |= n; else leds &= (uint16_t)~n;
}
HAL_StatusTypeDef HAL_TIM_PWM_Start(TIM_HandleTypeDef *t, uint32_t c) { (void)t; (void)c; return HAL_OK; }
void test_compare(TIM_HandleTypeDef *t, uint32_t c, uint32_t p) {
  pulses[t->id * 3 + c / 4] = p;
  int moving = 0; for (int i = 0; i < 6; ++i) if (pulses[i]) ++moving;
  CHECK(moving <= 1);
}
HAL_StatusTypeDef HAL_UART_Receive_IT(UART_HandleTypeDef *u, uint8_t *p, uint16_t n) {
  (void)n; rx[u->id] = p; u->RxState = HAL_UART_STATE_BUSY_RX; return HAL_OK;
}
HAL_StatusTypeDef HAL_UART_Transmit(UART_HandleTypeDef *u, const uint8_t *p, uint16_t n, uint32_t timeout) {
  (void)timeout; size_t len = strlen(tx[u->id]);
  if (len + n < sizeof(tx[0])) { memcpy(tx[u->id] + len, p, n); tx[u->id][len + n] = 0; }
  return HAL_OK;
}
uint32_t __get_PRIMASK(void) { return mask; }
void __disable_irq(void) { mask = 1; }
void __set_PRIMASK(uint32_t m) { mask = m; }
void Error_Handler(void) { puts("Unexpected HAL error"); exit(2); }
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *u);
static void feed(UART_HandleTypeDef *u, const char *s) {
  while (*s) { *rx[u->id] = (uint8_t)*s++; u->RxState = HAL_UART_STATE_READY; HAL_UART_RxCpltCallback(u); }
}

int main(void)
{
  const struct { const char *input, *reply; } cases[] = {
    {"PING","OK:PING"}, {"DRAWER:OPEN:1","OK:DRAWER"}, {"DRAWER:OPEN:6","OK:DRAWER"},
    {"LED:1:ON","OK:LED"}, {"LED:6:OFF","OK:LED"}, {"LED:ALL:OFF","OK:LED"},
    {"DRAWER:OPEN:0","ERR:DRAWER:RANGE"}, {"DRAWER:OPEN:7","ERR:DRAWER:RANGE"},
    {"DRAWER:OPEN:-1","ERR:DRAWER:RANGE"}, {"DRAWER:OPEN:99999999999999999","ERR:DRAWER:RANGE"},
    {"LED:0:ON","ERR:LED:RANGE"}, {"LED:7:OFF","ERR:LED:RANGE"}, {"LED:-1:ON","ERR:LED:RANGE"},
    {"LED:99999999999999999:OFF","ERR:LED:RANGE"},
    {"ping","ERR:UNKNOWN"}, {"drawer:OPEN:1","ERR:UNKNOWN"}, {"DRAWER:CLOSE:1","ERR:UNKNOWN"},
    {"DRAWER:OPEN:","ERR:UNKNOWN"}, {"DRAWER:OPEN:1:2","ERR:UNKNOWN"}, {"DRAWER:OPEN: 1","ERR:UNKNOWN"},
    {"DRAWER:OPEN:+1","ERR:UNKNOWN"}, {"DRAWER:OPEN:1.0","ERR:UNKNOWN"},
    {"LED:ALL:ON","ERR:UNKNOWN"}, {"LED:1:on","ERR:UNKNOWN"}, {"LED:1:BLINK","ERR:UNKNOWN"},
    {"LED::ON","ERR:UNKNOWN"}, {"LED:1:ON ","ERR:UNKNOWN"}, {"LED:1:ON:2","ERR:UNKNOWN"},
    {"PING:1","ERR:UNKNOWN"}, {"HOME","ERR:UNKNOWN"}, {"","ERR:UNKNOWN"}
  };
  for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
    Command cmd = {0}; char reply[32];
    ParseResult r = cmd_parse(cases[i].input, &cmd);
    cmd_make_reply(r, &cmd, reply, sizeof(reply));
    CHECK(strcmp(reply, cases[i].reply) == 0);
  }
  Command cmd = {0};
  CHECK(cmd_parse("DRAWER:OPEN:3", &cmd) == PARSE_OK && cmd.drawer == 3);
  CHECK(cmd_parse("LED:4:ON", &cmd) == PARSE_OK && cmd.drawer == 4 && cmd.led_on == 1);
  CHECK(cmd_parse("LED:4:OFF", &cmd) == PARSE_OK && cmd.led_on == 0);

  UART_HandleTypeDef u0 = {0,0}, u1 = {0,1}; char line[32];
  serial_cmd_init(&u0); serial_cmd_init(&u1);
  feed(&u0, "PING\r\nLED:1:ON\n"); feed(&u1, "DRAWER:OPEN:3\n");
  CHECK(serial_cmd_read_line(&u0,line) == SERIAL_LINE_OK && strcmp(line,"PING") == 0);
  CHECK(serial_cmd_read_line(&u1,line) == SERIAL_LINE_OK && strcmp(line,"DRAWER:OPEN:3") == 0);
  CHECK(serial_cmd_read_line(&u0,line) == SERIAL_LINE_OK && strcmp(line,"LED:1:ON") == 0);
  CHECK(serial_cmd_read_line(&u0,line) == SERIAL_NO_LINE);
  feed(&u0,"1234567890123456789012345678901\n");
  CHECK(serial_cmd_read_line(&u0,line) == SERIAL_LINE_OK && strlen(line) == 31);
  feed(&u0,"12345678901234567890123456789012\nPING\n");
  CHECK(serial_cmd_read_line(&u0,line) == SERIAL_LINE_TOO_LONG);
  CHECK(serial_cmd_read_line(&u0,line) == SERIAL_LINE_OK && strcmp(line,"PING") == 0);
  feed(&u0,"PI\001NG\n"); CHECK(serial_cmd_read_line(&u0,line) == SERIAL_LINE_TOO_LONG);
  *rx[0] = 0; HAL_UART_RxCpltCallback(&u0); feed(&u0,"PING\n");
  CHECK(serial_cmd_read_line(&u0,line) == SERIAL_LINE_TOO_LONG);
  feed(&u0,"\n"); CHECK(serial_cmd_read_line(&u0,line) == SERIAL_LINE_OK && line[0] == 0);
  for (int i = 0; i < 10; ++i) feed(&u0,"PING\n");
  for (int i = 0; i < 8; ++i) CHECK(serial_cmd_read_line(&u0,line) == SERIAL_LINE_OK);
  for (int i = 0; i < 2; ++i) CHECK(serial_cmd_read_line(&u0,line) == SERIAL_LINE_TOO_LONG);
  CHECK(serial_cmd_read_line(&u0,line) == SERIAL_NO_LINE);
  feed(&u0,"DRAWER:OPEN:"); u0.RxState = HAL_UART_STATE_READY;
  CHECK(serial_cmd_read_line(&u0,line) == SERIAL_NO_LINE);
  feed(&u0,"3\nPING\n");
  CHECK(serial_cmd_read_line(&u0,line) == SERIAL_LINE_TOO_LONG);
  CHECK(serial_cmd_read_line(&u0,line) == SERIAL_LINE_OK && strcmp(line,"PING") == 0);
  mask = 1; feed(&u0,"PING\n"); serial_cmd_read_line(&u0,line); CHECK(mask == 1); mask = 0;
  serial_cmd_send(&u1,"OK:PING"); CHECK(strcmp(tx[1],"OK:PING\n") == 0 && tx[0][0] == 0);

  TIM_HandleTypeDef t3 = {0}, t4 = {1}; drawer_init(&t3,&t4);
  CHECK(leds == 0); for (int i = 0; i < 6; ++i) CHECK(pulses[i] == 0);
  drawer_open(1); drawer_poll(); CHECK(leds == LED_1_Pin && pulses[0] == 2000);
  tick = 499; drawer_poll(); CHECK(pulses[0] == 2000);
  tick = 500; drawer_poll(); CHECK(pulses[0] == 1000);
  drawer_open(6); CHECK(leds == LED_6_Pin);
  drawer_open(3); CHECK(leds == LED_3_Pin);
  tick = 999; drawer_poll(); CHECK(pulses[0] == 1000 && pulses[2] == 0);
  tick = 1000; drawer_poll(); CHECK(pulses[0] == 0 && pulses[2] == 2000);
  tick = 1500; drawer_poll(); CHECK(pulses[2] == 1000);
  tick = 2000; drawer_poll(); CHECK(pulses[2] == 0);
  tick = 10499; drawer_poll(); CHECK(leds == LED_3_Pin);
  tick = 10500; drawer_poll(); CHECK(leds == 0);
  drawer_led(4,1); drawer_led(2,0); CHECK(leds == LED_4_Pin);
  drawer_led(4,0); CHECK(leds == 0);
  drawer_open(6); drawer_poll(); CHECK(pulses[5] == 2000);
  drawer_open(2); CHECK(pulses[5] == 1000 && pulses[1] == 0 && leds == LED_2_Pin);
  tick += 500; drawer_poll(); CHECK(pulses[5] == 0 && pulses[1] == 2000);
  tick += 500; drawer_poll(); tick += 500; drawer_poll();
  tick = UINT32_MAX - 100U; drawer_open(4); drawer_poll(); tick = 399; drawer_poll();
  CHECK(pulses[3] == 1000); tick = 899; drawer_poll(); CHECK(pulses[3] == 0);
  tick = 9899; drawer_poll(); CHECK(leds == 0);

  tick = 0; emergency_button_init();
  button = GPIO_PIN_RESET; CHECK(emergency_button_poll() == 0);
  tick = 10; button = GPIO_PIN_SET; CHECK(emergency_button_poll() == 0);
  tick = 20; button = GPIO_PIN_RESET; CHECK(emergency_button_poll() == 0);
  tick = 69; CHECK(emergency_button_poll() == 0);
  tick = 70; CHECK(emergency_button_poll() == 1);
  tick = 5000; CHECK(emergency_button_poll() == 0);
  button = GPIO_PIN_SET; CHECK(emergency_button_poll() == 0); tick += 50; CHECK(emergency_button_poll() == 0);
  button = GPIO_PIN_RESET; CHECK(emergency_button_poll() == 0); tick += 50; CHECK(emergency_button_poll() == 1);
  printf("%d / %d checks passed\n", total - failures, total);
  return failures ? 1 : 0;
}
