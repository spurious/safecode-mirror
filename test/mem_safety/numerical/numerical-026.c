/* Multiplication overflow leads to buffer allocation of size 0. */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

int main()
{
  char *buf;
  int16_t amt;
  int i, size;

  size = 0;
  amt = 1;

  for (i = 0; i < 16; i++)
  {
    size += amt;
    amt *= 2;
  }

  buf = malloc(size + 1);
  buf[0] = '0';
  free(buf);

  return 0;
}
