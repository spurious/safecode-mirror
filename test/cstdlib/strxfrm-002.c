// RUN: test.sh -p -t %t %s
#include <string.h>
#include <assert.h>
#include <locale.h>

// Example of the correct usage of strxfrm().

int main()
{
  char dst[10];
  char *source = "This is a string";
  char *good   = "This is a";
  setlocale(LC_ALL, "C");
  size_t sz;
  sz = strxfrm(dst, source, 9);
  assert(memcmp(good, dst, 10) == 0);
  return 0;
}
