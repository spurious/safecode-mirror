/*
 * RUN: sh test.sh %s
 */

/* strncat() with overlapping strings but with nothing copied.
 * This should pass. */

#include <string.h>
#include <stdio.h>

int main()
{
  char string[100] = "This is a string.";
  strncat(string, &string[10], 0);
  printf("%s\n", string);
  return 0;
}
