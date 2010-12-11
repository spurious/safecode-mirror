/* Reference count overflow leads to an object being prematurely free'd.
 */

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <setjmp.h>

typedef struct {
  void *data;
  uint16_t refcount;
} object;

void retain(object *o, int amt)
{
  o->refcount += amt;
}

void release(object *o, int amt)
{
  o->refcount -= amt;
  if (o->refcount == 0)
  {
    free(o->data);
  }
}

void use_object(object *o)
{
  int i;
  for (i = 0; i < 65536; i++)
  {
    *(int *)o->data = i;
    retain(o, 1);
  }
  for (i = 0; i < 65536; i++)
    release(o, 1);
}

int main()
{
  object o;
  o.data = malloc(sizeof(int));
  o.refcount = 1;
  use_object(&o);
  release(&o, 1);
  printf("refcount = %i\n", o.refcount);
  return 0;
}
