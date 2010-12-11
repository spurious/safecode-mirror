/* Overflow of variable amt leads to buffer of negative size being
 * reallocated. */

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

void extend(char *string, int16_t *amt)
{
  *amt += strlen(string);
}

#define NUM_STRINGS 33
#define STRING_SIZE 1000

int main()
{
  int16_t amt;
  int i;
  char strings[NUM_STRINGS][STRING_SIZE];
  char *final_string;
  final_string = malloc(amt = 1);
  final_string[0] = '\0';
  for (i = 0; i < NUM_STRINGS; i++)
  {
    strings[i][STRING_SIZE - 1] = '\0';
    memset(strings[i], 'a' + (i % 26), STRING_SIZE - 1);
    extend(strings[i], &amt);
    final_string = realloc(final_string, amt);
    strcat(final_string, strings[i]);
  }
  free(final_string);
  return 0;
}
