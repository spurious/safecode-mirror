/* Unsigned to signed conversion error leads to out-of-bounds table
 * access. */
#include <stdint.h>
#include <stdio.h>

#define SIZE         100
#define ENTRY -2147483645

int table[SIZE];

int get_entry(uint32_t index)
{
  int32_t u;
  u = index;
  if (u >= SIZE)
    return -1;
  else
    return table[u];
}

int main()
{
  int i;
  for (i = 0; i < SIZE; i++)
    table[i] = i % 2;
  if (get_entry(ENTRY) == 0)
    printf("%u is even\n", ENTRY);
  else
    printf("%u is odd\n", ENTRY);
  return 0;
}
