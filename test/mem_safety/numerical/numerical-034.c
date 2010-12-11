/* An array holding negative integers is looped through until a region
 * of the array is made positive. The loop counter can overflow leading
 * to an infinite loop which waits until an index outside the range is
 * made positive. */

#include <stdint.h>

#define ARRSZ 100000
#define AREASZ  1000


void increment_range(int8_t *array, int start, int end);
void make_region_positive(int8_t *array, int start, int end);

void make_region_positive(int8_t *array, int start, int end)
{
  int16_t index;
  for (index = start; index <= end; index++)
    while (array[index] <= 0)
      increment_range(array, start, end);
}

void increment_range(int8_t *array, int start, int end)
{
  int index;
  for (index = start; index <= end; index++)
    array[index]++;
}

int main()
{
  int8_t array[ARRSZ];
  int i;
  for (i = 0; i < ARRSZ; i++)
    array[i] = - (i / AREASZ);
  make_region_positive(&array[0], 0, 10000);
  make_region_positive(&array[30000], 10000, 50000);
  return 0;
}
