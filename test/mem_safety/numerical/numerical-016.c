/* Overflow of reference count leads to use after free. */

#include <stdint.h>
#include <string.h>
#include <stdlib.h>

#define ITERATIONS 512

typedef struct
{
  uint8_t reference_count;
} test;

void check_to_free(test *t)
{
  if (t->reference_count == 0)
    free(t);
}

void use(test *t)
{
  int i;
  for (i = 0; i < ITERATIONS; i++)
  {
    t->reference_count++;
    check_to_free(t);
  }
}

int main()
{
  test *t;
  t = malloc(sizeof(test));
  t->reference_count = 0;
  use(t);
  return 0;
}
