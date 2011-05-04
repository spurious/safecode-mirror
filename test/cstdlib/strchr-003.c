// RUN: test.sh -e -t %t %s

// Call strchr() on an unterminated tail.

#include <string.h>

int main()
{
  char string[6] = "\00012345";
  strchr(&string[1], '5');
  return 0;
}
