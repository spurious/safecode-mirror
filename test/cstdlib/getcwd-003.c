// RUN: test.sh -p -t %t %s

#include <assert.h>
#include <unistd.h>
#include <string.h>

//
// Use getcwd() to get a path successfully.
//

int main() {
  char buf[5], *cwd;

  chdir("/tmp");

  cwd = getcwd(buf, sizeof(buf));

  assert(cwd != NULL);
  assert(strcmp(buf, "/tmp") == 0);

  return 0;
}
