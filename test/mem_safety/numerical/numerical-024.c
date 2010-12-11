/* Underflow of counter. */

#include <stdint.h>
#include <stdio.h>

int16_t semaphore;

int main()
{
  int count;
  semaphore = 0;

  for (count = 0; count < 50000; count++)
    semaphore--;

  if (semaphore < 0)
    printf("negative\n");
  else
    printf("nonnegative\n");
  return 0;
}
