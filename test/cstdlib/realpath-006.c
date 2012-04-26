// RUN: test.sh -e -t %t %s

#include <stdlib.h>
#include <unistd.h>

int main()
{
  char * buffer = malloc (4);
  char * p = realpath ("/etc/passwd", buffer);
  printf ("%s\n", p);
  return 0;
}
