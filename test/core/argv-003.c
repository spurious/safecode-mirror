// RUN: test.sh -p -t %t %s silly
//
// TEST: argv-003
//
// Description:
//  Test that array bounds checking works on argv strings
//

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int
main (int argc, char ** argv) {
  int index = 0;
  for (index = 0; index < strlen (argv[0]); ++index) {
    printf ("%c", argv[0][index]);
  }

  return 0;
}

