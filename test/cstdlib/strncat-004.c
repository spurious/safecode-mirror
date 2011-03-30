/*
 * RUN: sh test.sh %s
 * XFAIL: *
 */

/* Concatenation onto unterminated destination string. */

#include <string.h>

void do_cat(const char *src)
{
  char f[] = { 'm', 'e', 'o', 'w' };
  strncat(f, src, 2);
}

int main()
{
  do_cat("cat");
  return 0;
}
