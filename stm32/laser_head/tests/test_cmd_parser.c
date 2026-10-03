/* cmd_parser PC 테스트 (보드 없이 실행)
 *
 * 빌드 · 실행 (stm32/laser_head 폴더에서):
 *   gcc -I Core/Inc tests/test_cmd_parser.c Core/Src/cmd_parser.c -o test_cmd_parser
 *   ./test_cmd_parser
 */
#include <stdio.h>
#include <string.h>
#include "cmd_parser.h"

typedef struct {
  const char *input;
  const char *expected;
} TestCase;

static const TestCase cases[] = {
  /* 정상 명령 */
  { "PING",        "OK:PING" },
  { "AIM:90,45",   "OK:AIM" },
  { "AIM:0,0",     "OK:AIM" },
  { "AIM:180,180", "OK:AIM" },
  { "LASER:ON",    "OK:LASER" },
  { "LASER:OFF",   "OK:LASER" },
  { "HOME",        "OK:HOME" },
  /* 범위 밖 → RANGE */
  { "AIM:181,90",  "ERR:AIM:RANGE" },
  { "AIM:90,200",  "ERR:AIM:RANGE" },
  { "AIM:-1,90",   "ERR:AIM:RANGE" },
  { "AIM:99999,0", "ERR:AIM:RANGE" },
  /* 형식 오류 · 모르는 명령 → UNKNOWN */
  { "aim:90,45",   "ERR:UNKNOWN" },   /* 소문자 */
  { "AIM:90",      "ERR:UNKNOWN" },
  { "AIM:90,",     "ERR:UNKNOWN" },
  { "AIM:,45",     "ERR:UNKNOWN" },
  { "AIM:90,45,1", "ERR:UNKNOWN" },
  { "AIM:9A,45",   "ERR:UNKNOWN" },
  { "AIM: 90,45",  "ERR:UNKNOWN" },   /* 공백 */
  { "LASER:BLINK", "ERR:UNKNOWN" },
  { "LASER",       "ERR:UNKNOWN" },
  { "PING:1",      "ERR:UNKNOWN" },
  { "HELLO",       "ERR:UNKNOWN" },
  { "",            "ERR:UNKNOWN" },
};

int main(void)
{
  int total = (int)(sizeof(cases) / sizeof(cases[0]));
  int failed = 0;

  for (int i = 0; i < total; i++) {
    Command cmd = {0};
    char reply[32];
    ParseResult result = cmd_parse(cases[i].input, &cmd);
    cmd_make_reply(result, &cmd, reply, sizeof(reply));

    int ok = strcmp(reply, cases[i].expected) == 0;
    if (!ok) {
      failed++;
    }
    printf("[%s] %-14s -> %-14s (expected %s)\n",
           ok ? "PASS" : "FAIL", cases[i].input, reply, cases[i].expected);
  }

  /* AIM 값이 제대로 들어갔는지 */
  Command cmd = {0};
  if (cmd_parse("AIM:120,45", &cmd) != PARSE_OK || cmd.pan != 120 || cmd.tilt != 45) {
    printf("[FAIL] AIM:120,45 -> pan=%d tilt=%d\n", cmd.pan, cmd.tilt);
    failed++;
  } else {
    printf("[PASS] AIM:120,45 -> pan=120 tilt=45\n");
  }
  total++;

  printf("\n%d / %d passed\n", total - failed, total);
  return failed == 0 ? 0 : 1;
}
