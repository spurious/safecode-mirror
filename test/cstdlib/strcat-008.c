/*
 * RUN: sh test.sh %s
 * XPASS: *
 */

/* strcat() with nothing copied although strings overlap. Should pass. */
#include <string.h>

int main()
{
  char string[7] = "string";
  strcat(string, &string[6]);
  return 0;
}
