/* Integer overflow leads to incorrect allocation. */

#include <stdint.h>
#include <stdlib.h>

int *get_buffer(int offset)
{
  int16_t base;
  base = 10;
  base += offset;
  return calloc(base, sizeof(int));
}

int main()
{
  int *array;
  array = get_buffer(65550);
  array[24] = 100;
  free(array);
  return 0;
}
