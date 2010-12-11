/* Signed to unsigned conversion error. */

#include <stdint.h>
#include <stdio.h>

unsigned is_number_odd(int num)
{
  return (num % 2);
}

int main()
{
  unsigned is_odd;
  int number;
  number = -3;
  is_odd = is_number_odd(number);
  if (is_odd == 1)
    printf("%i is odd\n", number);
  else
    printf("%i is even\n", number);
  return 0;
}
