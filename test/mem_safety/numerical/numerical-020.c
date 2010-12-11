/* Underflow in multiplication of negative by positive. */

#include <stdint.h>
#include <stdio.h>

int main()
{
  int16_t num1 = -129;
  int16_t num2 = 256;
  int16_t result = num1 * num2;
  printf("%i * %i = %i\n", num1, num2, result);
}
