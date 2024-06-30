#include <endian.h>
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

#define ETF_MAGIC         (131)
#define SMALL_INTEGER_EXT (97)
#define INTEGER_EXT       (98)
#define SMALL_TUPLE_EXT   (104)
#define NIL_EXT           (106)
#define STRING_EXT        (107) // followed by two bytes of length (big-endian)
#define LIST_EXT          (108) // followed by four bytes of length
#define BINARY_EXT        (109) // followed by four bytes of length

#define KEY_STORE (33)
#define KEY_MAX (KEY_STORE-1)

enum {
  exa_nil,
  exa_list,
  exa_binary
} exa_type;

struct exa_tuple {
  uint8_t type;
  char key[KEY_STORE];
  uint8_t count;
  int32_t number;
  uint8_t *blob;
  int32_t *list;
  int len;
  int error;
};

#define CR "\n\r"

/*

port = Port.open({:spawn, "./crex1"}, [:binary])
data = :erlang.term_to_binary({})
Port.command(port, data)
Port.command(port, :erlang.term_to_binary({}))
Port.close()

{}
131, 104, 0

{"info"}
131, 104, 1, 109, 0, 0, 0, 4, 105, 110, 102, 111

{"get-outs", []}
131 104 002 109 000 000 000 008 103 101 116 045 111 117 116 115 106
ETF STU LEN BIN L3  L2  L1  L0  g   e   t   -   o   u   t   s   NIL

{"ret-outs", [0, 1, 2]}
131 104 002 109 000 000 000 008 114 101 116 045 111 117 116 115 107 000 003 000 001 002
ETF STU LEN BIN L3  L2  L1  L0  r   e   t   -   o   u   t   s   STE L1  L0  1   2   3

{"get-outs", [1023]}
131 104 002 109 000 000 000 008 103 101 116 045 111 117 116 115 108 000 000 000 001 098 000 000 003 255 106
ETF STU LEN BIN L3  L2  L1  L0  r   e   t   -   o   u   t   s   LIE L3  L2  L1  L0  INE 1023            NIL

{"get-outs",[1, 1023]}
131 104 002 109 000 000 000 008 103 101 116 045 111 117 116 115 108 000 000 000 002 097 001 098 000 000 003 255 106
ETF STU LEN BIN L3  L2  L1  L0  r   e   t   -   o   u   t   s   LIE L3  L2  L1  L0  SIE 1   INE 1023            NIL

{"info","okay"}
131 104 002 109 000 000 000 004 105 110 102 111 109 000 000 000 004 111 107 097 121
ETF STU LEN BIN L3  L2  L1  L0  i   n   f   o   BIN L3  L2  L1  L0  o   k   a   y
*/

int free_list_fail(struct exa_tuple *kv, int r) {
  if (kv) {
    if (kv->list) {
      free(kv->list);
      kv->blob = NULL;
    }
    kv->len = 0;
  }
  return r;
}

int receiver(int fd, struct exa_tuple *kv) {
  LOG("receiver"CR);

  if (fd < 0) return -1;
  if (!kv) return -1;
  
  kv->type = exa_nil;
  kv->count = 0;
  if (kv->list) free(kv->list);
  kv->list = NULL;
  if (kv->blob) free(kv->blob);
  kv->blob = NULL;
  kv->len = 0;

  uint8_t b;
  uint32_t u;
  int32_t i;
  uint16_t s;

  int r;
  
  // match magic
  r = read(fd, &b, sizeof(b));
  if (r < 0) return r;
  if (b != ETF_MAGIC) return -1;
  LOG("ETF_MAGIC"CR);

  // match small tuple
  r = read(fd, &b, sizeof(b));
  if (r < 0) return r;
  if (b != SMALL_TUPLE_EXT) return -1;
  LOG("SMALL_TUPLE_EXT"CR);

  // match len == 2
  r = read(fd, &b, sizeof(b));
  if (r < 0) return r;
  //if (b != 2) return -1;
  kv->count = b;
  LOG("tuple count == %d"CR, kv->count);
  if (kv->count == 0) {
    return 0;
  }

  // match bin
  r = read(fd, &b, sizeof(b));
  if (r < 0) return r;
  if (b != BINARY_EXT) return -1;
  LOG("BINARY_EXT"CR);

  // match 4 byte len
  r = read(fd, &u, sizeof(u));
  if (r != 4) return -1;

  uint32_t len = be32toh(u);
  uint32_t skip = 0;

  LOG("len = %d"CR, len);

  if (len > KEY_MAX) {
    skip = len - KEY_MAX;
    len = KEY_MAX;
    LOG("adjusted to len = %d"CR, len);
  }
  r = read(fd, kv->key, len);
  if (r != len) return -1;
  if (skip) {
    LOG("skipping %d bytes of key"CR, skip);
    uint8_t ignore;
    for (int i=0; i<skip; i++) {
      r = read(fd, &ignore, sizeof(ignore));
      if (r < 0) return r;
    }
  }

  LOG("key = %s"CR, kv->key);
  if (kv->count == 1) {
    return 0;
  }

  // next we can get
  // NIL = no list follows
  // STE = simple < 65536 elements of bytes
  // LIE = list of SIE and INE then NIL
  // BIN = binary blob
  
  // decide next thing

  r = read(fd, &b, sizeof(b));
  if (r < 0) return r;

  switch (b) {
    case NIL_EXT:
      LOG("NIL_EXT"CR);
      kv->type = exa_nil;
      return 0;
      break;
    case BINARY_EXT:
      LOG("BINARY_EXT"CR);
      kv->type = exa_binary;
      r = read(fd, &u, sizeof(u));
      if (r != 4) return -1;
      len = be32toh(u);
      LOG("blob len = %d"CR, len);
      kv->len = len;
      if (len) {
        kv->blob = (uint8_t *)malloc((len+1) * sizeof(uint8_t));
        if (kv->blob) {
          r = read(fd, kv->blob, len);
          if (r < 0) {
            free(kv->blob);
            kv->blob = NULL;
            return r;
          }
          kv->blob[len] = '\0';
          LOG("blob <<%s>>"CR, kv->blob);
          return 0;
        }
      } return 0;
      break;
    case STRING_EXT:
      LOG("STRING_EXT"CR);
      kv->type = exa_list;
      r = read(fd, &s, sizeof(s));
      if (r != 2) return -1;
      len = be16toh(s);
      LOG("list len = %d"CR, len);
      kv->len = len;
      if (len) {
        kv->list = (int32_t *)malloc(len * sizeof(int32_t));
        if (kv->list) {
          for (int i=0; i<len; i++) {
            r = read(fd, &b, sizeof(b));
            if (r < 0) return free_list_fail(kv, r);
            LOG("[%d] = %d"CR, i, b);
            kv->list[i] = b;
          }
          return 0;
        }
      }
      break;
    case LIST_EXT:
      LOG("LIST_EXT"CR);
      kv->type = exa_list;
      r = read(fd, &u, sizeof(u));
      if (r != 4) return -1;
      len = be32toh(u);
      LOG("list len = %d"CR, len);
      kv->len = len;
      if (len) {
        kv->list = (int32_t *)malloc(len * sizeof(int32_t));
        if (kv->list) {
          for (int i=0; i<len; i++) {
            int32_t n = 0;
            r = read(fd, &b, sizeof(b));
            if (r != 1) return free_list_fail(kv, -1);
            if (b == SMALL_INTEGER_EXT) {
              LOG("SMALL_INTEGER_EXT"CR);
              r = read(fd, &b, sizeof(b));
              if (r != 1) return free_list_fail(kv, -1);
              n = b;
            } else if (b == INTEGER_EXT) {
              LOG("INTEGER_EXT"CR);
              r = read(fd, &u, sizeof(u));
              if (r != 4) return free_list_fail(kv, -1);
              n = u;
            } else {
              LOG("unexpected %d"CR, b);
              return free_list_fail(kv, -1);
            }
            LOG("[%d] = %d"CR, i, n);
            kv->list[i] = n;
          }
          r = read(fd, &b, sizeof(b));
          if (r != 1) return free_list_fail(kv, -1);
          if (b != NIL_EXT) {
            LOG("expected NIL_EXT"CR);
            return free_list_fail(kv, -1);
          }
          LOG("NIL_EXT"CR);
          return 0;
        } return -1;
      }
      break;
    default:
      LOG("unexpected %d"CR, b);
      return -1;
  }

  return -1;
}

void cleaner(void) {
  LOG("cleaner()"CR);
  fflush(stdout);
  fflush(stderr);
}

int main(int argc, char *argv[]) {
  atexit(cleaner);
  int i;

#if 0
  do {
    i = ascending();
    LOG("got %d"CR, i);
  } while (i != -1);
  ccrContext z = 0;

  do {
    i = descending(&z, -1);
    LOG("got %d"CR, i);
  } while (z);

  uint8_t buffer[1024];
  int array[256];
  int array_length;

  // Read the encoded data from stdin (Elixir port)
  // int bytes_read = read(STDIN_FILENO, buffer, sizeof(buffer));
  // if (bytes_read < 0) {
  //   LOG("read error %s\n\r", strerror(errno));
  //   return -1;
  // }
#endif

  struct exa_tuple kv;
  kv.blob = NULL;
  kv.list = NULL;
  kv.type = exa_nil;
  kv.len = 0;
  kv.count = 0;

  do {
    i = receiver(STDIN_FILENO, &kv);
  } while (i >= 0);

  LOG("exit with i=%d"CR, i);

  if (kv.blob) free(kv.blob);
  if (kv.list) free(kv.list);

  fflush(stderr);
  fflush(stdout);

  return 0;
}
