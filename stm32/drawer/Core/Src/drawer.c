#include "drawer.h"

/* Temporary values: calibrate with the assembled mechanism before use. */
#define SERVO_RETURN_PULSE_US 1900U
#define SERVO_PUSH_PULSE_US   1400U
#define SERVO_PUSH_MS         500U
#define SERVO_RETURN_MS       1000U
#define LED_TIMEOUT_MS        10000U

#define PCA9685_ADDRESS        (0x40U << 1)
#define PCA9685_MODE1          0x00U
#define PCA9685_MODE2          0x01U
#define PCA9685_LED0_ON_L      0x06U
#define PCA9685_PRESCALE       0xFEU
#define PCA9685_PRESCALE_50HZ  121U

static I2C_HandleTypeDef *pca_i2c;
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
static uint8_t pca_i2c_ok;

static int pca9685_read(uint8_t reg, uint8_t *data, uint16_t size)
{
  return HAL_I2C_Mem_Read(pca_i2c, PCA9685_ADDRESS, reg,
                          I2C_MEMADD_SIZE_8BIT, data, size, 100) == HAL_OK;
}

static void pca9685_write(uint8_t reg, uint8_t value)
{
  if (HAL_I2C_Mem_Write(pca_i2c, PCA9685_ADDRESS, reg,
                        I2C_MEMADD_SIZE_8BIT, &value, 1, 100) != HAL_OK) {
    pca_i2c_ok = 0;
  }
}

static int pulse(int n, uint32_t us)
{
  uint8_t data[4] = {0, 0, 0, 0};
  uint8_t reg = (uint8_t)(PCA9685_LED0_ON_L + 4U * (uint32_t)(n - 1));

  if (us == 0U) {
    data[3] = 0x10U; /* Full OFF bit. */
  } else {
    uint32_t count = (us * 4096U + 10000U) / 20000U;
    if (count > 4095U) count = 4095U;
    data[2] = (uint8_t)(count & 0xFFU);
    data[3] = (uint8_t)((count >> 8) & 0x0FU);
  }

  if (HAL_I2C_Mem_Write(pca_i2c, PCA9685_ADDRESS, reg,
                        I2C_MEMADD_SIZE_8BIT, data, sizeof(data), 100) != HAL_OK) {
    pca_i2c_ok = 0;
    return 0;
  }

  uint8_t check[4];
  if (!pca9685_read(reg, check, sizeof(check))) {
    pca_i2c_ok = 0;
    return 0;
  }
  for (uint32_t i = 0; i < sizeof(data); ++i) {
    if (check[i] != data[i]) {
      pca_i2c_ok = 0;
      return 0;
    }
  }
  return 1;
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

void drawer_init(I2C_HandleTypeDef *i2c)
{
  pca_i2c = i2c;
  pca_i2c_ok = 1;
  pca9685_write(PCA9685_MODE1, 0x10U); /* Sleep before changing prescale. */
  pca9685_write(PCA9685_PRESCALE, PCA9685_PRESCALE_50HZ);
  pca9685_write(PCA9685_MODE2, 0x04U); /* Totem-pole output. */
  pca9685_write(PCA9685_MODE1, 0x20U); /* Wake with auto-increment. */
  HAL_Delay(1);
  pca9685_write(PCA9685_MODE1, 0xA0U); /* Restart with auto-increment. */

  phase = SERVO_IDLE;
  active = pending = 0;
  drawer_led_all_off();
  for (int n = 1; n <= 6; ++n) {
    pulse(n, 0); /* Keep startup stationary until calibrated. */
  }
}

int drawer_i2c_check(void)
{
  uint8_t mode1, mode2, prescale;
  if (!pca9685_read(PCA9685_MODE1, &mode1, 1) ||
      !pca9685_read(PCA9685_MODE2, &mode2, 1) ||
      !pca9685_read(PCA9685_PRESCALE, &prescale, 1)) {
    return 0;
  }

  return pca_i2c_ok &&
         (mode1 & 0x30U) == 0x20U &&
         (mode2 & 0x04U) == 0x04U &&
         prescale == PCA9685_PRESCALE_50HZ;
}

int drawer_open(int n)
{
  if (n < 1 || n > 6 || phase != SERVO_IDLE || pending) return 0;
  drawer_led(n, 1);
  pending = n;
  return 1;
}

DrawerEvent drawer_poll(void)
{
  uint32_t now = HAL_GetTick();
  if (lit && (uint32_t)(now - led_since) >= LED_TIMEOUT_MS) drawer_led_all_off();
  if (phase == SERVO_PUSH && (uint32_t)(now - servo_since) >= SERVO_PUSH_MS) {
    if (!pulse(active, SERVO_RETURN_PULSE_US)) {
      (void)pulse(active, 0);
      active = pending = 0;
      phase = SERVO_IDLE;
      return DRAWER_EVENT_I2C_ERROR;
    }
    phase = SERVO_RETURN;
    servo_since = now;
  } else if (phase == SERVO_RETURN && (uint32_t)(now - servo_since) >= SERVO_RETURN_MS) {
    if (!pulse(active, 0)) {
      active = pending = 0;
      phase = SERVO_IDLE;
      return DRAWER_EVENT_I2C_ERROR;
    }
    active = 0;
    phase = SERVO_IDLE;
    return DRAWER_EVENT_COMPLETE;
  }
  if (phase == SERVO_IDLE && pending) {
    active = pending;
    pending = 0;
    if (!pulse(active, SERVO_PUSH_PULSE_US)) {
      (void)pulse(active, 0);
      active = 0;
      return DRAWER_EVENT_I2C_ERROR;
    }
    servo_since = now;
    phase = SERVO_PUSH;
  }
  return DRAWER_EVENT_NONE;
}
