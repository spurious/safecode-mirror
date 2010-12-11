/* Overflow in custom strcpy() leads incomplete copy of data and/or out
 * of bounds copy. */

#include <stdint.h>
#include <string.h>

void _strcpy(char *dst, char *src)
{
  int16_t index;
  index = 0;
  while (src[index] != '\0')
    dst[index] = src[index++];
}

#define SZ 40000

int main()
{
  char buf1[SZ], buf2[SZ];
  memset(buf2, 'a', SZ - 1);
  buf2[SZ - 1] = '\0';
  _strcpy(buf1, buf2);
  return 0;
}
