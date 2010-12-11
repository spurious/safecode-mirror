/* Signed to unsigned conversion leads to too many values being printed. */

#include <stdint.h>
#include <stdio.h>

void print_few_times(uint16_t times)
{
  uint16_t i;
  for (i = 0; i < times; i++)
    printf("Printed.\n");
}

int get_amount()
{
  return 0;
}

int main()
{
  int amount;
  amount = get_amount();
  print_few_times(amount - 1);
  return 0;
}
