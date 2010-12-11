/* Truncation error leads to wrong file descriptor being written to. */

#include <unistd.h>
#include <stdint.h>

int main()
{
  int8_t outfd;
  int fd[2], amt_written, c;
  char output[] = "You should never see this.\n";

  pipe(fd);
  /* Outfd is truncated to 1. This will have the effect of writing to
     stdout. */
  outfd = dup2(fd[1], 257);
  amt_written = 0;
  while (amt_written < sizeof(output))
  {
    c = write(outfd, &output[amt_written], sizeof(output) - amt_written);
    if (c == -1)
      continue;
    amt_written += c;
  }

  close(fd[1]);
  close(fd[0]);
  return 0;
}
