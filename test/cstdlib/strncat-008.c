/*
 * RUN: test.sh %s
 */

/* strncat() where the source array does not overlap with the destination
 * string but they share the same terminator. This should pass, since the
 * items to be copied do not overlap. */

#include <string.h>

int main()
{
  char string[20] = "a string:";
  strncat(&string[1], string, 1);
  return 0;
}
