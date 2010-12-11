/* Counter overflow leads to incorrect count. */

#include <stdint.h>
#include <stdio.h>

int16_t times_called = 0;

void call_func()
{
  times_called++;
}

int main()
{
  int i;
  for (i = 0; i < 65535; i++)
    call_func();
  printf("The function was called %i times.\n", times_called);
  return 0;
}
