/*
 * RUN: test.sh %s
 * XFAIL: *
 */

/* Call strchr() on an unterminated tail. */

#include <string.h>
#include <stdio.h>

int main()
{
  char string[6] = "\00012345";
  printf("%p\n", strchr(&string[1], '5'));
  return 0;
}
