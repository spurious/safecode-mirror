// RUN: test.sh -s "call void @fastlscheck_debug" -p -t %t %s

/* byval test */

// #include "stdio.h"

struct bvt {
   int i;
   int j;
   char str[10];
};

void byval_test(struct bvt t)
{

   t.i += 2;
   t.j += 4;
   t.str[0] = 'H';

   // printf("t = {%d, %d, %s}.\n", t.i, t.j, t.str);

   return;
}

int main()
{
   struct bvt t = {0, 1, "hello"};

   // printf("t = {%d, %d, %s}.\n", t.i, t.j, t.str);
   byval_test(t);
   // printf("t = {%d, %d, %s}.\n", t.i, t.j, t.str);

   return(0);
}
