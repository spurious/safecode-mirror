/* Signed to unsigned conversion error leads to string being printed where
 * it should not be. */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

void print_at_most(const char *string, unsigned int amt)
{
  char front[] = "One string: ", *buffer;
  buffer = malloc(amt + 1 + strlen(front));
  strcpy(buffer, front);
  strncat(buffer, string, amt);
  printf("%s\n", buffer);
  free(buffer);
}

int main()
{
  char array[] = { 'N', 'o', ' ', '\\', '0', ' ', '!' };
  print_at_most("You should see only some of this.", 20);
  print_at_most("You should see this.", 40);
  print_at_most("You shouldn't see this.", 0);
  print_at_most(array, -1);
  return 0;
}
