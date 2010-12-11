/* Truncation error. Reports an incorrect location for the first
 * substring "bbb" in a long string. */

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

int8_t first_substr(const char *substr, const char *string)
{
  char *loc;
  loc = strstr(string, substr);
  if (loc == NULL)
    return -1;
  else
    return loc - string;
}

#define STRSZ 140

int main()
{
  char string[STRSZ];
  memset(string, ' ', STRSZ - 4);
  strcpy(&string[STRSZ - 4], "BBB");
  printf("First instance of \"BBB\" in \"%s\" is at %i\n.", \
    string, first_substr("BBB", string));
  return 0;
}
