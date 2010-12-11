/* Print a progression of numbers tracking their sum and terminating
 * when their sum exceeds a certain value. Infinite loop occurs when overflow
 * leads to the values of the progression never advancing beyond 0. */

#include <stdint.h>
#include <stdio.h>

void print_progression(int start, int delta, int max)
{
  int16_t state;
  int sum;
  state = start;
  sum = 0;
  while (sum < max)
  {
    printf("%i\n", state);
    sum += state;
    state += delta;
  }
}

int main()
{
  printf("Terms in [1, 3, 5, 7, ...] that sum to under 4000:\n");
  print_progression(1, 2, 4000);

  printf("\nTerms in [0, 65536, 131072, ...] that sum to under " \
    "1000000:\n");
  print_progression(0, 65536, 1000000);

  return 0;
}
