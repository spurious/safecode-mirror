/* Waits until random() returns an integer at most a certain amount.
 * Due to an unsigned to signed conversion, the event will never happen
 * for large inputs.
 */

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>

int get_random_at_most(uint32_t max)
{
  int32_t bound;
  int    result;
  bound = max;
  do result = random();
    while (result > bound);
  return result;
}

int main()
{
  printf("%i\n", get_random_at_most(50));
  printf("%i\n", get_random_at_most(4294967295u));
  return 0;
}
