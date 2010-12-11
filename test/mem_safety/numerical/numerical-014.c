/* Multiplication leads to buffer size overflow which leads to
 * allocation of too small a buffer. */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define BUFSZ        1000
#define MULTIPLIER    132

int main()
{
  char *string;
  char *string2;
  int16_t size;

  size = BUFSZ;

  string = malloc(BUFSZ);
  memset(string, 'a', BUFSZ - 1);
  string[BUFSZ - 1] = '\0';

  size *= MULTIPLIER;
  // size should be 928
  string2 = malloc(size);

  strcpy(string2, string);
  printf("%s\n", string2);

  free(string);
  free(string2);
  return 0;
}
