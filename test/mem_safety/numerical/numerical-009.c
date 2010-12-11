/* Signed to unsigned conversion error in string length calculation
 * leads to too large of a buffer being copied. */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

char* strings[] = {
  "26 abcdefghijklmnopqrstuvwxyz",
  "10 0123456789",
  "-1 abcd",
  NULL
} ;

static int position = 0;

const char *next_string()
{
  return strings[position++];
}

uint16_t get_info(const char *string, int *position)
{
  int sz;
  sscanf(string, "%i %n", &sz, position);
  return (uint16_t) sz;
}

int main()
{
  char string[1000];
  const char *ptr;
  int pos, size;
  string[0] = '\0';
  while ((ptr = next_string()) != NULL)
  {
    size = get_info(ptr, &pos);
    printf("string %s has size %i\n", &ptr[pos], size);
    strncpy(string, &ptr[pos], size + 1);
  }
  return 0;
}
