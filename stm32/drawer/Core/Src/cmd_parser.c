#include "cmd_parser.h"
#include <stdio.h>
#include <string.h>

/* HAL-independent parser. Saturate numbers to avoid integer overflow. */
static const char *number(const char *p, int *value)
{
  int sign = 1, n = 0;
  if (*p == '-') { sign = -1; ++p; }
  if (*p < '0' || *p > '9') return NULL;
  while (*p >= '0' && *p <= '9') {
    if (n < 1000) n = n * 10 + (*p - '0');
    ++p;
  }
  *value = sign * n;
  return p;
}

ParseResult cmd_parse(const char *line, Command *cmd)
{
  if (strcmp(line, "PING") == 0) { cmd->type = CMD_PING; return PARSE_OK; }
  if (strcmp(line, "LED:ALL:OFF") == 0) {
    cmd->type = CMD_LED_ALL_OFF;
    return PARSE_OK;
  }
  if (strcmp(line, "I2C:CHECK") == 0) {
    cmd->type = CMD_I2C_CHECK;
    return PARSE_OK;
  }
  const char *p;
  int n;
  if (strncmp(line, "DRAWER:OPEN:", 12) == 0) {
    p = number(line + 12, &n);
    if (p == NULL || *p != '\0') return PARSE_UNKNOWN;
    cmd->type = CMD_DRAWER;
  } else if (strncmp(line, "LED:", 4) == 0) {
    p = number(line + 4, &n);
    if (p == NULL) return PARSE_UNKNOWN;
    if (strcmp(p, ":ON") == 0) cmd->led_on = 1;
    else if (strcmp(p, ":OFF") == 0) cmd->led_on = 0;
    else return PARSE_UNKNOWN;
    cmd->type = CMD_LED;
  } else return PARSE_UNKNOWN;
  if (n < 1 || n > 6) return PARSE_RANGE;
  cmd->drawer = n;
  return PARSE_OK;
}

void cmd_make_reply(ParseResult result, const Command *cmd, char *out, size_t size)
{
  const char *name = cmd->type == CMD_PING ? "PING" :
                     cmd->type == CMD_DRAWER ? "DRAWER" :
                     cmd->type == CMD_I2C_CHECK ? "I2C" : "LED";
  if (result == PARSE_OK && cmd->type == CMD_PING) snprintf(out, size, "OK@PING");
  else if (result == PARSE_OK) snprintf(out, size, "OK:%s", name);
  else if (result == PARSE_RANGE) snprintf(out, size, "ERR:%s:RANGE", name);
  else snprintf(out, size, "ERR:UNKNOWN");
}
