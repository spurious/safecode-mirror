/*
 * RUN: test.sh %s
 * XFAIL: *
 */

/* strrchr() with an unterminated string searching for a character not
 * that is found in the string. */

#include <stdio.h>
#include <string.h>

int main()
{
  char a[1000];
  memset(a, 'a', 1000);
  printf("%p\n", strrchr(a, 'a'));
  return 0;
}
