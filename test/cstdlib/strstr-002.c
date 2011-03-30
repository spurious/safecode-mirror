/*
 * RUN: sh test.sh %s
 * XFAIL: *
 */

/* strstr() with an unterminated substring. */

#include <string.h>
#include <stdio.h>

int main()
{
  char substring[] = { 'a', 'b', 'c' };
  char string[] = "abcdefg";

  printf("%p\n", strstr(string, substring));
  return 0;
}
