/* Truncation error in buffer allocation leads to buffer overflow. */

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

void *allocate(int16_t sz)
{
  return malloc(sz);
}

#define ALLOC_SIZE 70000

int main()
{
  char *str;
  str = allocate(ALLOC_SIZE);
  bzero(str, ALLOC_SIZE);
  free(str);
  return 0;
}
