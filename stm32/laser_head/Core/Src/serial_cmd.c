#include "serial_cmd.h"
#include <string.h>

/* UART 하나의 수신 상태 */
typedef struct {
  UART_HandleTypeDef *huart;
  uint8_t rx_byte;                    /* 인터럽트로 받은 1바이트 */
  char rx_buf[SERIAL_LINE_MAX];       /* 모으는 중인 줄 */
  uint8_t rx_len;
  uint8_t rx_overflow;                /* 지금 줄이 너무 길었는지 */
  char line[SERIAL_LINE_MAX];         /* 완성된 줄 (메인 루프가 가져감) */
  volatile SerialLineStatus status;   /* 인터럽트와 메인 루프가 같이 쓰므로 volatile */
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
  HAL_UART_Receive_IT(huart, &p->rx_byte, 1);
}

SerialLineStatus serial_cmd_read_line(UART_HandleTypeDef *huart, char *out)
{
  SerialPort *p = find_port(huart);
  if (p == NULL) {
    return SERIAL_NO_LINE;
  }

  /* 수신 오류(오버런 등)로 수신이 멈췄으면 다시 시작 */
  if (huart->RxState == HAL_UART_STATE_READY) {
    HAL_UART_Receive_IT(huart, &p->rx_byte, 1);
  }

  SerialLineStatus status = p->status;
  if (status == SERIAL_NO_LINE) {
    return SERIAL_NO_LINE;
  }
  if (status == SERIAL_LINE_OK) {
    strcpy(out, p->line);
  }
  /* 다 읽은 뒤에 비워야 인터럽트가 다음 줄을 넣을 수 있음 */
  p->status = SERIAL_NO_LINE;
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
    /* 메인 루프가 이전 줄을 아직 안 가져갔으면 이번 줄은 버림 */
    if (p->status == SERIAL_NO_LINE) {
      if (p->rx_overflow) {
        p->status = SERIAL_LINE_TOO_LONG;
      } else if (p->rx_len > 0) {
        memcpy(p->line, p->rx_buf, p->rx_len);
        p->line[p->rx_len] = '\0';
        p->status = SERIAL_LINE_OK;
      }
    }
    p->rx_len = 0;
    p->rx_overflow = 0;
  } else if (c != '\r') {               /* '\r'은 무시 (protocol.md 1장) */
    if (p->rx_len < SERIAL_LINE_MAX - 1) {
      p->rx_buf[p->rx_len++] = c;
    } else {
      p->rx_overflow = 1;
    }
  }

  /* 다음 1바이트 수신 예약 */
  HAL_UART_Receive_IT(huart, &p->rx_byte, 1);
}
