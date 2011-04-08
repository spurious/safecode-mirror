/*
 * RUN: test.sh %s
 * XFAIL: *
 */

/* Destination string written to out of bounds. */

#include <string.h>

int main()
{
  char a[10];
  char b[500];
  a[0] = '\0';
  memset(b, 'b', 10);
  strncat(a, b, 50);
  return 0;
}
