/*
 * RUN: sh test.sh %s
 * XFAIL: *
 */

/* strpbrk() searching on an unterminated string. */

#include <string.h>
#include <stdio.h>

int main()
{
  char a[100];
  memset(a, 'a', 100);
  printf("%p\n", strpbrk(a, "ab"));
  return 0;
}
