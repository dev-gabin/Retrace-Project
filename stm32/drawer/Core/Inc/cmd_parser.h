#ifndef CMD_PARSER_H
#define CMD_PARSER_H
#include <stddef.h>
typedef enum { CMD_PING, CMD_DRAWER, CMD_LED, CMD_LED_ALL_OFF } CmdType;
typedef enum { PARSE_OK, PARSE_UNKNOWN, PARSE_RANGE } ParseResult;
typedef struct {
  CmdType type;
  int drawer; /* 1..6 */
  int led_on;
} Command;
ParseResult cmd_parse(const char *line, Command *cmd);
void cmd_make_reply(ParseResult result, const Command *cmd, char *out, size_t size);
#endif
