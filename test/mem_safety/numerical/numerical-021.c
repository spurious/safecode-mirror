/* Truncation error in sscanf leads to incorrect result being printed.
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define BFSZ 20
#define STEP 100

char buffer[BFSZ];

void increment()
{
  int i;
  int16_t v;
  sscanf(buffer, "%i", &i);
  v = i;
  v += STEP;
  i = v;
  snprintf(buffer, BFSZ, "%i", i);
}

void initialize()
{
  strcpy(buffer, "0");
}

int16_t get_value()
{
  int i;
  int16_t v;
  sscanf(buffer, "%i", &i);
  v = i;
  return v;
}

int main()
{
  int i;
  initialize();
  for (i = 0; i < 400; i++)
    increment();
  printf("%i\n", (int) get_value());
  return 0;
}
