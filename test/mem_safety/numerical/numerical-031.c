/* rot13() transforms a string in place. The loop counter is of smaller
 * width than the length of the string, so an infinite loop occurs on
 * large strings due to overflow and the comparison always being false. */

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

char *rot13(const char *string, int length)
{
  uint16_t index;
  char *result;
  result = malloc(length + 1);
  for (index = 0; index < length; index++)
  {
    if (string[index] >= 'a' && string[index] <= 'm' || \
          string[index] >= 'A' && string[index] <= 'M')
    {
      result[index] = string[index] + 13;
    }
    else if (string[index] >= 'n' && string[index] <= 'z' ||
              string[index] >= 'N' && string[index] <= 'Z')
    {
      result[index] = string[index] - 13;
    }
    else
      result[index] = string[index];
  }
  result[length] = '\0';
  return result;
}

#define STR2SZ 70000

int main()
{
  char string1[] = "Hello world", string2[STR2SZ];
  char *result1, *result2;
  memset(string2, 'A', STR2SZ - 1);
  string2[STR2SZ - 1] = '\0';

  result1 = rot13(string1, sizeof(string1) - 1);
  printf("ROT13(%s) = %s\n", string1, result1);
  free(result1);

  result2 = rot13(string2, sizeof(string2) - 1);
  printf("ROT13(%s) = %s\n", string2, result2);
  free(result2);

  return 0;
}
