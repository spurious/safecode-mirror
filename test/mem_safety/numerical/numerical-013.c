/* Overflow leads to out-of-bounds write on a buffer. */

#include <stdio.h>
#include <stdint.h>

char string[128];

void store(char c, int8_t index)
{
  string[index] = c;
}

int main()
{
  int i;
  char test[] = "This is a test. This is a test. This is a test.\n" \
                "This is a test. This is a test. This is a test.\n" \
                "This is a test. This is a test. This is a test.\n";
  for (i = 0; i < sizeof(test); i++)
    store(test[i], i);
  printf("%s\n", string);
  return 0;
}
