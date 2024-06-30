#include <errno.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "coroutine.h"

#define LOG(...) fprintf(stderr, __VA_ARGS__)

int ascending(void) {
  static int i;
  scrBegin;
  for (i=0; i<10; i++) {
    scrReturn(i);
  }
  scrFinish(-1);
}

int descending(ccrContParam, int fd) {
  ccrBeginContext;
  int i;
  ccrEndContext(foo);
  ccrBegin(foo);
  for (foo->i=10; foo->i>=0; foo->i--) {
    ccrReturn(foo->i);
  }
  ccrFinish(-1);
}

int receiver(ccrContParam, int fd, uint8_t *buffer, int len) {
  ccrBeginContext;
  int i;
  ccrEndContext(foo);
  ccrBegin(foo);

  do {
    foo->i++;
  } while (foo->i<len);
  
  receiver_done:

  ccrFinish(-1);
}


int main(int argc, char *argv[]) {
  int i;

  do {
    i = ascending();
    LOG("got %d\n", i);
  } while (i != -1);
  ccrContext z = 0;

  do {
    i = descending(&z, -1);
    LOG("got %d\n", i);
  } while (z);

  uint8_t buffer[1024];
  int array[256];
  int array_length;

  // Read the encoded data from stdin (Elixir port)
  int bytes_read = read(STDIN_FILENO, buffer, sizeof(buffer));
  if (bytes_read < 0) {
    LOG("read error %s\n\r", strerror(errno));
    return -1;
  }
  
  do {
    i = receiver(&z, STDIN_FILENO, buffer, bytes_read);
    LOG("got %d\n", i);
  } while (z);

  return 0;
}
