#include "cmd_parser.h"
#include <stdio.h>
#include <string.h>

#define ANGLE_MIN  0
#define ANGLE_MAX  180

/* s에서 정수 하나를 읽어 *value에 저장하고, 숫자 바로 다음 위치를 돌려줌.
 * 숫자가 없으면 NULL. 음수('-')도 읽어서 범위 검사에서 RANGE로 걸러지게 함 */
static const char *parse_number(const char *s, int *value)
{
  int negative = 0;
  int n = 0;
  int digits = 0;

  if (*s == '-') {
    negative = 1;
    s++;
  }
  while (*s >= '0' && *s <= '9') {
    if (n < 1000) {               /* 아주 큰 수도 1000 이상으로만 유지 (int 넘침 방지) */
      n = n * 10 + (*s - '0');
    }
    digits++;
    s++;
  }
  if (digits == 0) {
    return NULL;
  }
  *value = negative ? -n : n;
  return s;
}

static int angle_ok(int angle)
{
  return angle >= ANGLE_MIN && angle <= ANGLE_MAX;
}

ParseResult cmd_parse(const char *line, Command *cmd)
{
  if (strcmp(line, "PING") == 0) {
    cmd->type = CMD_PING;
    return PARSE_OK;
  }
  if (strcmp(line, "HOME") == 0) {
    cmd->type = CMD_HOME;
    return PARSE_OK;
  }
  if (strcmp(line, "LASER:ON") == 0) {
    cmd->type = CMD_LASER;
    cmd->laser_on = 1;
    return PARSE_OK;
  }
  if (strcmp(line, "LASER:OFF") == 0) {
    cmd->type = CMD_LASER;
    cmd->laser_on = 0;
    return PARSE_OK;
  }
  if (strncmp(line, "AIM:", 4) == 0) {
    int pan;
    int tilt;
    const char *p = parse_number(line + 4, &pan);
    if (p == NULL || *p != ',') {
      return PARSE_UNKNOWN;
    }
    p = parse_number(p + 1, &tilt);
    if (p == NULL || *p != '\0') {
      return PARSE_UNKNOWN;
    }
    cmd->type = CMD_AIM;
    if (!angle_ok(pan) || !angle_ok(tilt)) {
      return PARSE_RANGE;
    }
    cmd->pan = pan;
    cmd->tilt = tilt;
    return PARSE_OK;
  }
  return PARSE_UNKNOWN;
}

static const char *cmd_name(CmdType type)
{
  switch (type) {
    case CMD_PING:  return "PING";
    case CMD_AIM:   return "AIM";
    case CMD_LASER: return "LASER";
    case CMD_HOME:  return "HOME";
  }
  return "";
}

void cmd_make_reply(ParseResult result, const Command *cmd, char *out, size_t out_size)
{
  switch (result) {
    case PARSE_OK:
      snprintf(out, out_size, "OK:%s", cmd_name(cmd->type));
      break;
    case PARSE_RANGE:
      snprintf(out, out_size, "ERR:%s:RANGE", cmd_name(cmd->type));
      break;
    default:
      snprintf(out, out_size, "ERR:UNKNOWN");
      break;
  }
}
