/* Read from the wrong pipe due to truncation error. */

#include <unistd.h>
#include <stdint.h>
#include <stdio.h>

#define FDTBLSZ 350

int fdtable[FDTBLSZ][2];

void read_index(uint8_t idx, char *buf, int bufsz)
{
  while (read(fdtable[idx][0], buf, bufsz) < 0)
    ;
}

int main()
{
  int i;
  char *str1 = "Earth", *str2 = "Mars";
  char buf[10];

  for (i = 0; i < FDTBLSZ; i++)
  {
    pipe(fdtable[i]);
    if (i < 100)
      write(fdtable[i][1], str2, sizeof(str2));
    else
      write(fdtable[i][1], str1, sizeof(str1));
  }

  read_index(300, buf, 10);
  printf("We are on the planet %s.\n", buf);

  for (i = 0; i < FDTBLSZ; i++)
  {
    close(fdtable[i][0]);
    close(fdtable[i][1]);
  }

  return 0;
}
