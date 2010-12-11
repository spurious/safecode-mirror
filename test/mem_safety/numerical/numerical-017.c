/* Unsigned to signed conversion error leads to out of bounds hash table
 * access. */

#include <stdint.h>
#include <string.h>

const char *hash_table[256];

uint8_t _hash(const char *key)
{
  int i;
  uint8_t result;
  result = 0;
  for (i = 0; i < strlen(key); i++)
    result ^= key[i];
  return result;
}

void insert(const char *str)
{
  int8_t index;
  index = _hash(str);
  hash_table[index] = str;
}

int main()
{
  insert("hello");
  insert("Test.");
  insert("\x80");
  return 0;
}
