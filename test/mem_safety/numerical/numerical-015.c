/* Truncation error leads to out of bounds array access. */

#include <stdint.h>

char string[300];

int main()
{
  char *string_pointer;
  int16_t index;
  string_pointer = &string[-10000];
  index = 10002;
  string_pointer[(int8_t) index] = 'a';
  return 0;
}
