/* Unsigned to signed conversion error leads to allocation of a buffer
 * of invalid size. */

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

uint8_t get_malloc_size()
{
  return 255;
}

int main()
{
  int8_t malloc_size;
  char *string;

  malloc_size = get_malloc_size();
  string = malloc(malloc_size);
  string[0] = '\0';
  return 0;
}
