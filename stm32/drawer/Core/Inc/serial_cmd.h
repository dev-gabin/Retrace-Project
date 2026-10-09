#ifndef SERIAL_CMD_H
#define SERIAL_CMD_H

#include "main.h"

/* protocol.md 1장: 한 줄 최대 32바이트 ('\n' 포함) */
#define SERIAL_LINE_MAX       32
#define SERIAL_PORT_COUNT     2     /* 등록할 수 있는 UART 개수 */
#define SERIAL_TX_TIMEOUT_MS  50

typedef enum {
  SERIAL_NO_LINE = 0,     /* 아직 완성된 줄 없음 */
  SERIAL_LINE_OK,         /* 한 줄 받음 */
  SERIAL_LINE_TOO_LONG    /* 긴 줄 · 비정상 문자 · 수신 오류 · 큐 초과 → ERR@UNKNOWN */
} SerialLineStatus;

/* UART를 등록하고 인터럽트 수신 시작 */
void serial_cmd_init(UART_HandleTypeDef *huart);

/* 메인 루프에서 호출. 완성된 줄이 있으면 out에 복사 ('\n', '\r' 제외) */
SerialLineStatus serial_cmd_read_line(UART_HandleTypeDef *huart, char *out);

/* msg 뒤에 '\n'을 붙여서 전송 */
void serial_cmd_send(UART_HandleTypeDef *huart, const char *msg);

#endif /* SERIAL_CMD_H */
