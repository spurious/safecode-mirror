/* A loop that tracks how many iterations through an array of unsigned
 * numbers are required for the running sum to exceed some maximum
 * will become an infinite loop on some inputs. */

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>

int how_many_iterations(unsigned *array, int sz, unsigned max)
{
  uint32_t sum;
  int i, iterations;
  sum = 0;
  iterations = 0;
  while (sum < max)
  {
    for (i = 0; i < sz; i++)
    {
      iterations++;
      sum += array[i];
      if (sum > max)
        break;
    }
  }
  return iterations;
}

void print_results(unsigned int array[], int sz, unsigned max)
{
  int how_many, i;
  how_many = how_many_iterations(array, sz, max);
  printf("We require %i iterations for the running sum of [ ", how_many);
  for (i = 0; i < sz; i++)
  {
    printf("%u", array[i]);
    if (i != sz - 1)
      printf(", ");
  }
  printf(" ] to exceed %u.\n", max);
}

int main()
{
  unsigned int array1[] = { 3, 4, 5, 6, 7 } ;
  unsigned int array2[] = { 1, 0 } ;
  unsigned int array3[] = { 1000, 1000, 4294965296u };
  print_results(array1, 5, 10);
  print_results(array2, 2, 1000);
  print_results(array3, 3, 4294967295u);
  return 0;
}
