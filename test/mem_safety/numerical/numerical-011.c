/* Underflow in buffer index leads to out-of-bounds writes on a
 * character array. */

#include <stdint.h>
#include <stdlib.h>

void zero_backwards(char *buf, int sz)
{
  int16_t index;
  index = 0;
  while (sz--)
    buf[index--] = '\0';
}

#define SIZE 60000

int main()
{
  char *buf, *end;
  buf = malloc(SIZE);
  end = &buf[SIZE - 1];
  zero_backwards(end, SIZE);
  free(buf);
  return 0;
}
