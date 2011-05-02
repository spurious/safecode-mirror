/* RUN: test.sh -e -t %t %s */

/* Call index() on an unterminated tail. */

#include <strings.h>

int main()
{
  char string[6] = "\00012345";
  index(&string[1], '5');
  return 0;
}
