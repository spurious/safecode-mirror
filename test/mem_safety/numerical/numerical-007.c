/* Truncation error leads to too small buffer being allocated to hold a
 * string. */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BASE_SZ 30

void *get_buffer(int16_t extension)
{
  return malloc(BASE_SZ + extension);
}

int main()
{
  int32_t size;
  char *string;
  char string2[BASE_SZ] = "This is less than 30.";
  size = 65520;
  string = get_buffer(size);
  memcpy(string, string2, BASE_SZ);
  printf("%s\n", string);
  free(string);
  return 0;
}
