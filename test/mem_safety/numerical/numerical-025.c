/* Addition overflow. */

#include <stdint.h>
#include <stdio.h>

#define ENTRIES 1000000

int entries[ENTRIES];

int main()
{
  int i, index;
  int16_t sum;
  for (i = 0; i < ENTRIES; i++)
    entries[i] = i;
  sum = 0;
  index = 1;
  for (i = 0; i < 20; i++)
  {
    sum += entries[index];
    index *= 2;
  }
  printf("Sum of first 20 powers of 2 is %i\n", sum);
  return 0;
}
