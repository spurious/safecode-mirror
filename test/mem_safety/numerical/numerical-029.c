/* Signed to unsigned conversion error leads to incorrect least element
 * calculation. */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

void print_least_element(int8_t *array, int size)
{
  int i;
  uint8_t least;
  for (i = 0, least = 255; i < size; i++)
    if (array[i] < least)
      least = array[i];
  printf("Least element of [");
  for (i = 0; i < size; i++)
  {
    printf("%i", array[i]);
    if (i < size - 1)
      printf(", ");
  }
  printf("] is %i.\n", least);
}

int main()
{
  int8_t array1[] = { 10, 120, 30, 40 };
  int8_t array2[] = { -9,  5, 64, 21, -111 };
  print_least_element(array1, sizeof(array1));
  print_least_element(array2, sizeof(array2));
  return 0;
}
