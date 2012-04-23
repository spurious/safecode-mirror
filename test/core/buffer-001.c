// RUN: test.sh -e -t %t %s
//
// TEST: buffer-001
//
// Description:
//  Test that an off-by-one read on a global is detected.
//

#include <stdio.h>
#include <stdlib.h>

char array[1024];

int
main (int argc, char ** argv) {
  return array[1024];
}

