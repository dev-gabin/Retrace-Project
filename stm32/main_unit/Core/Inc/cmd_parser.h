#ifndef CMD_PARSER_H
#define CMD_PARSER_H

#include <stddef.h>

/* HAL을 쓰지 않는 순수 C 코드 → PC에서도 테스트 가능 (tests/test_cmd_parser.c) */

/* 레이저 헤드가 받는 명령 (protocol.md 1장, 2장) */
typedef enum {
  CMD_PING,
  CMD_AIM,
  CMD_LASER,
  CMD_HOME
} CmdType;

typedef enum {
  PARSE_OK,         /* 올바른 명령 */
  PARSE_UNKNOWN,    /* 모르는 명령 · 형식 오류 → ERR:UNKNOWN */
  PARSE_RANGE       /* 형식은 맞지만 값이 범위 밖 → ERR:AIM:RANGE */
} ParseResult;

typedef struct {
  CmdType type;
  int pan;          /* AIM: 0~180 */
  int tilt;         /* AIM: 0~180 */
  int laser_on;     /* LASER: 1 = ON, 0 = OFF */
} Command;

/* line: '\n'을 뺀 한 줄 (예: "AIM:90,45") */
ParseResult cmd_parse(const char *line, Command *cmd);

/* 파싱 결과로 응답 문자열 생성 (예: "OK:AIM", "ERR:AIM:RANGE", "ERR:UNKNOWN") */
void cmd_make_reply(ParseResult result, const Command *cmd, char *out, size_t out_size);

#endif /* CMD_PARSER_H */
