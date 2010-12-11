/* The process() function loops through an array of command structures which
 * tell it to read and write certain amounts of data. An overflow leads to a
 * read of data on an empty pipe and so the command blocks.
 */

#include <stdint.h>
#include <unistd.h>
#include <string.h>

#define BUFSZ 66000
#define CUTOFF 100

struct command {
  enum { READ, WRITE, STOP } what;
  char buf[10];
  int fd;
  int amt;
};

int fds[4];

void process(struct command *commands, uint16_t start)
{
  uint16_t pos;
  pos = start;
  int amt, total;
  while (commands[pos].what != STOP)
  {
    switch (commands[pos].what)
    {
      case READ:
        /* We must read the full amount to continue. */
        total = 0;
        do
        {
          amt = read(commands[pos].fd, commands[pos].buf, commands[pos].amt);
          if (amt != -1)
            total += amt;
        } while (total != commands[pos].amt);
        break;
      case WRITE:
        write(commands[pos].fd, commands[pos].buf, commands[pos].amt);
        break;
      default:
        break;
    }
    pos++;
  }
}

int main()
{
  struct command commands[BUFSZ];
  int i;
  
  pipe(&fds[0]);
  pipe(&fds[2]);
  /* The first 100 requests deal with reads/write to the first set of
     descriptors. */
  for (i = 0; i < CUTOFF; i++)
  {
    if (i % 2 == 0)
    {
      commands[i].what = READ;
      commands[i].amt  = 10;
      commands[i].fd   = fds[0];
    }
    else
    {
      commands[i].what = WRITE;
      commands[i].amt  = 10;
      commands[i].fd   = fds[1];
      strcpy(commands[i].buf, "123456789");
    }
  }
  /* The remaining requests deal with the second set of file descriptors. */
  for (i = CUTOFF + 1; i < BUFSZ - 1; i++)
  {
    if (i % 2 == 0)
    {
      commands[i].what = READ;
      commands[i].amt  = 10;
      commands[i].fd   = fds[2];
    }
    else
    {
      commands[i].what = WRITE;
      commands[i].amt  = 10;
      commands[i].fd   = fds[3];
      strcpy(commands[i].buf, "thestring");
    }
  }
  /* Tell loop to stop. */
  commands[BUFSZ - 1].what = STOP;
  /* Process the command array starting at the cutoff. */
  process(commands, CUTOFF + 1);
  close(fds[0]);
  close(fds[1]);
  close(fds[2]);
  close(fds[3]);
  return 0;
}
