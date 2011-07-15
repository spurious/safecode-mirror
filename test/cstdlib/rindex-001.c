// RUN: test.sh -c -e -t %t %s

// rindex() with an unterminated string searching for a character not
// that is found in the string.

#include <strings.h>

int main()
{
  char a[1000];
  memset(a, 'a', 1000);
  rindex(a, 'a');
  return 0;
}
