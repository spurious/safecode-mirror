/* traverse() traverses an array of integers until it finds an element
 * valued -1. The traversal follows the values of the array. The
 * index used by the traversal is too small to hold all these values. */

#include <stdint.h>
#include <string.h>
#include <stdio.h>

void traverse(int *array, int start)
{
  int8_t ptr;
  ptr = start;
  while (array[ptr] != -1)
    ptr = array[ptr];
}

#define ARR2SZ 2000

int main()
{
  int array[10]  = { 1, 2, 3, -1 };
  int array2[ARR2SZ] = { 0, 2, 5, 4, 1024, 3 };
  bzero(&array2[6], sizeof(int) * (ARR2SZ - 6));
  array2[1024] = -1;
  traverse(array, 0);
  traverse(array2, 1);
  return 0;
}
