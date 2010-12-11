/* Underflow in multiplication leads to out of bounds write on array. */

#include <stdint.h>
#include <stdio.h>

#define ITEMS_SZ 32768

int items[ITEMS_SZ];

int main()
{
  int16_t i;
  int count;
  int *ptr;

  ptr = &items[ITEMS_SZ - 1];
  for (i = -3, count = 0; count < 15; count++, i *= 2)
    ptr[i] = 0xffffffff;
  return 0;
}
