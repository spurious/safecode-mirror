/* Function that tries to combine two large strings ends up allocating
 * smaller than expected buffer due to overflow. */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

char *combine(char *string1, char *string2)
{
  int16_t length1, length2, total;
  char    *dest;
  length1 = strlen(string1);
  length2 = strlen(string2);
  total   = length1 + length2;
  dest = malloc(total);
  strcpy(dest, string1);
  strcat(dest, string2);
  return dest;
}

#define BUF1SZ 35000
#define BUF2SZ 35000

int main()
{
  char string1[BUF1SZ], string2[BUF2SZ];
  char *result;
  memset(string1, 'a', BUF1SZ - 1);
  string1[BUF1SZ - 1] = '\0';
  memset(string2, 'b', BUF2SZ - 1);
  string2[BUF2SZ - 1] = '\0';
  result = combine(string1, string2);
  free(result);
  return 0;
}
