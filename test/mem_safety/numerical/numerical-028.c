/* Truncation error leads to bad result being printed. */

#include <stdint.h>
#include <stdio.h>

void print_result(int8_t amount)
{
  printf("There are %i primes in [2, 1000].\n", amount);
}

int main()
{
  int i, j, count, start, end;
  count = 0;
  start = 2;
  end   = 1000;
  for (i = start; i <= end; i++)
  {
    for (j = 2; j < i; j++)
    {
      if (i % j == 0)
        break;
    }
    if (j == i)
      count++;
  }
  print_result(count);
  return 0;
}
