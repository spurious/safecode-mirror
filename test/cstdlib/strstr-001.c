/*
 * RUN: test.sh %s
 * XFAIL: *
 */

/* strstr() on unterminated superstring. */

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

int main()
{
  char m[100];
  char *s1 = malloc(100);
  memset(m, 'm', 100);
  strcpy(s1, "meow");
  printf("%p\n", strstr(m, s1));
  free(s1);
  return 0;
}
