/*
 * RUN: sh test.sh %s
 * XFAIL: *
 */

/* Call strchr() on an unterminated string, and the character to find
 * is not inside the string. */

#include <stdio.h>
#include <string.h>

int main()
{
  char a[1000];
  memset(a, 'a', 1000);
  printf("%p\n", strchr(a, 'b'));
  return 0;
}
