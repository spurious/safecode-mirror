/* A naive way to get a string duplicate by copying over increasingly many
   bytes until equal comparison will never finish on long sized string
   inputs that overflow the copy amount. */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

void copy_over(char **dest, const char *src, unsigned amt)
{
  *dest = malloc(amt + 1);
  memcpy(*dest, src, amt);
  (*dest)[amt] = '\0';
}

char *dup_(const char *src)
{
  uint8_t destlen;
  char *dest;
  destlen = 0;
  copy_over(&dest, src, destlen);
  while (strcmp(dest, src) != 0)
  {
    free(dest);
    copy_over(&dest, src, ++destlen);
  }
  return dest;
}

#define SZ 1000

int main()
{
  char *dest, src1[] = "hello world", src2[SZ];

  dest = dup_(src1);
  printf("%s\n", dest);
  free(dest);

  memset(src2, 'a', SZ - 1);
  src2[SZ - 1] = '\0';

  dest = dup_(src2);
  printf("%s\n", dest);
  free(dest);

  return 0;
}
