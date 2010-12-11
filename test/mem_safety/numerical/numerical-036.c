/* random_sum() generates a sum of random numbers that exceed a certain
 * minimum. The minimum may be greater than the bound of the 16 bit variable
 * used to keep the running total.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

unsigned random_sum(unsigned min)
{
  uint16_t sum;
  sum = 0;
  while (sum < min)
    sum += random();
  return sum;
}

int main()
{
  printf("%u\n", random_sum(500));
  printf("%u\n", random_sum(70000));
  return 0;
}
