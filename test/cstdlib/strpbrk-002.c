/*
 * RUN: sh test.sh %s
 * XFAIL: *
 */

/* strpbrk() on an unterminated set of characters to search for. */

#include <stdio.h>
#include <string.h>

int main()
{
  char set[] = { 'a', 'b', 'c' };
  printf("%p\n", strpbrk("string", set));
  return 0;
}
