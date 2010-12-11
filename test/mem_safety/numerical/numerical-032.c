/* Transfer a character buffer via pipes. Keep track of amount
 * transferred. Infinite loop occurs because truncation makes it seem
 * like 0 bytes are being transferred at a time. */

#include <unistd.h>
#include <stdint.h>
#include <stdio.h>

#define TRANSFER_CHUNK_SIZE 256
#define BUFSZ             10000

int fd[2];
char src_buf[BUFSZ];
char dest_buf[BUFSZ];

uint8_t write_bytes(char *bytes, int amt);
void transfer_text(char *text, int amt);

void transfer_text(char *text, int amt)
{
  int bytes_transferred, delta, amt_to_write;
  bytes_transferred = 0;
  while (bytes_transferred < amt)
  {
    amt_to_write = TRANSFER_CHUNK_SIZE;
    if (amt - bytes_transferred < amt_to_write)
      amt_to_write = amt - bytes_transferred;

    delta = write_bytes(&text[bytes_transferred], amt_to_write);
    read(fd[0], &dest_buf[bytes_transferred], amt_to_write);
    bytes_transferred += delta;
  }
}

uint8_t write_bytes(char *bytes, int amt)
{
  return write(fd[1], bytes, amt);
}

int main()
{
  pipe(fd);
  transfer_text(src_buf, BUFSZ);
  close(fd[0]);
  close(fd[1]);
  return 0;
}
