#include "serial_cmd.h"
#include <string.h>
#define SERIAL_QUEUE_SIZE 8U

/* UART 하나의 수신 상태 */
typedef struct {
  UART_HandleTypeDef *huart;
  uint8_t rx_byte;                    /* 인터럽트로 받은 1바이트 */
  char rx_buf[SERIAL_LINE_MAX];       /* 모으는 중인 줄 */
  uint8_t rx_len;
  uint8_t rx_overflow;                /* 지금 줄이 너무 길었는지 */
  char lines[SERIAL_QUEUE_SIZE][SERIAL_LINE_MAX];
  SerialLineStatus statuses[SERIAL_QUEUE_SIZE];
  volatile uint32_t head, tail;
  volatile uint32_t dropped;
} SerialPort;

static SerialPort ports[SERIAL_PORT_COUNT];
static uint8_t port_count = 0;

static SerialPort *find_port(UART_HandleTypeDef *huart)
{
  for (uint8_t i = 0; i < port_count; i++) {
    if (ports[i].huart == huart) {
      return &ports[i];
    }
  }
  return NULL;
}

void serial_cmd_init(UART_HandleTypeDef *huart)
{
  if (port_count >= SERIAL_PORT_COUNT) {
    return;
  }
  SerialPort *p = &ports[port_count++];
  memset(p, 0, sizeof(*p));
  p->huart = huart;
  if (HAL_UART_Receive_IT(huart, &p->rx_byte, 1) != HAL_OK) Error_Handler();
}

SerialLineStatus serial_cmd_read_line(UART_HandleTypeDef *huart, char *out)
{
  SerialPort *p = find_port(huart);
  if (p == NULL) {
    return SERIAL_NO_LINE;
  }

  /* 수신 오류(오버런 등)로 수신이 멈췄으면 다시 시작 */
  if (huart->RxState == HAL_UART_STATE_READY) {
    p->rx_len = 0;
    p->rx_overflow = 1; /* Damaged line must not execute a partial command. */
    if (HAL_UART_Receive_IT(huart, &p->rx_byte, 1) != HAL_OK) Error_Handler();
  }

  uint32_t mask = __get_PRIMASK();
  __disable_irq();
  SerialLineStatus status = SERIAL_NO_LINE;
  if (p->head != p->tail) {
    uint32_t slot = p->tail % SERIAL_QUEUE_SIZE;
    status = p->statuses[slot];
    if (status == SERIAL_LINE_OK) strcpy(out, p->lines[slot]);
    ++p->tail;
  } else if (p->dropped) {
    --p->dropped;
    status = SERIAL_LINE_TOO_LONG; /* Reject overflow instead of silently dropping. */
  }
  __set_PRIMASK(mask);
  return status;
}

void serial_cmd_send(UART_HandleTypeDef *huart, const char *msg)
{
  HAL_UART_Transmit(huart, (const uint8_t *)msg, (uint16_t)strlen(msg), SERIAL_TX_TIMEOUT_MS);
  HAL_UART_Transmit(huart, (const uint8_t *)"\n", 1, SERIAL_TX_TIMEOUT_MS);
}

/* HAL이 1바이트 수신을 끝낼 때마다 부르는 함수 (인터럽트 안에서 실행됨) */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
  SerialPort *p = find_port(huart);
  if (p == NULL) {
    return;
  }

  char c = (char)p->rx_byte;

  if (c == '\n') {
    if ((uint32_t)(p->head - p->tail) < SERIAL_QUEUE_SIZE) {
      uint32_t slot = p->head % SERIAL_QUEUE_SIZE;
      memcpy(p->lines[slot], p->rx_buf, p->rx_len);
      p->lines[slot][p->rx_len] = '\0';
      p->statuses[slot] = p->rx_overflow ? SERIAL_LINE_TOO_LONG : SERIAL_LINE_OK;
      ++p->head;
    } else if (p->dropped < UINT32_MAX) ++p->dropped;
    p->rx_len = 0;
    p->rx_overflow = 0;
  } else if (c != '\r') {               /* '\r'은 무시 (protocol.md 1장) */
    if ((uint8_t)c < 0x20 || (uint8_t)c > 0x7e) {
      p->rx_overflow = 1;
    } else if (p->rx_len < SERIAL_LINE_MAX - 1) {
      p->rx_buf[p->rx_len++] = c;
    } else {
      p->rx_overflow = 1;
    }
  }

  /* 다음 1바이트 수신 예약 */
  HAL_UART_Receive_IT(huart, &p->rx_byte, 1);
}
