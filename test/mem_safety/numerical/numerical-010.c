/* Overflow in table index leads to incorrect table access. */

#include <stdint.h>
#include <stdio.h>

#define TABLE_SIZE 400

int32_t table[TABLE_SIZE];

int get_item(int8_t index)
{
  return table[index];
}

int main()
{
  int i;
  for (i = 0; i < TABLE_SIZE; i++)
    table[i] = i * i;
  printf("130 ^ 2 = %i\n", get_item(130));
  return 0;
}
