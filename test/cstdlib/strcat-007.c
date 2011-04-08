/*
 * RUN: test.sh %s
 * XFAIL: *
 */

/* Concatenate onto a destination that is too short by one. */

#include <string.h>
#include <stdio.h>

int main()
{
  char a[11] = "the \0string";
  char b[] = "strings";
  strcat(a, b);
  printf("%s\n", a);
  return 0;
}
