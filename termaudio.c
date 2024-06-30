#include <errno.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define ETF_MAGIC         (131)
#define SMALL_INTEGER_EXT (97)
#define INTEGER_EXT       (98)
#define FLOAT_EXT         (99)
#define NIL_EXT           (106)
#define STRING_EXT        (107) // followed by two bytes of length (big-endian)
#define LIST_EXT          (108) // followed by four bytes of length
#define BINARY_EXT        (109) // followed by four bytes of length

/*

SMALL_TUPLE_EXT 104

BINARY_EXT 198

LIST_EXT 108
or
STRING_EXT 107

for

{"get-outs", []}

-> 131 104 002 109 000 000 000 008 103 101 116 045 111 117 116 115 106
   ETF STU LEN BIN L3  L2  L1  L0  g   e   t   -   o   u   t   s   NIL

{"ret-outs", [0, 1, 2]}

-> 131 104 002 109 000 000 000 008 114 101 116 045 111 117 116 115 107 000 003 000 001 002
   ETF STU LEN BIN L3  L2  L1  L0  r   e   t   -   o   u   t   s   STE L1  L0  1   2   3

*/

int32_t read_int32(const uint8_t *buffer) {
  return (buffer[0] << 24) | (buffer[1] << 16) | (buffer[2] << 8) | buffer[3];
}

uint32_t read_uint32(const uint8_t *buffer) {
  return (buffer[0] << 24) | (buffer[1] << 16) | (buffer[2] << 8) | buffer[3];
}

uint32_t read_uint16(const uint8_t *buffer) {
  return (buffer[0] << 8) | buffer[1];
}

// Function to decode an ETF-encoded list of integers, and in case the
// list is all integers < 256 with length < 65535, an ETF-encoded string

enum decode_etf_tuple_key_list {
  etf,
  
  stu,
  rl1,
  
  bin,
  rl4,

  str,
  rl2,
};

int decode_int_array(const uint8_t *buffer, int *array, int *array_length) {
  const uint8_t *p = buffer;
  
  if (*p != ETF_MAGIC) {
    fprintf(stderr, "Invalid ETF_MAGIC (%d) got (%d)\n\r", ETF_MAGIC, *p);
    return -1;
  }
  p++;

  int mode = -1;
  uint32_t list_length = 0;

  switch (*p) {
    case STRING_EXT:
      mode = *p++;
      list_length = read_uint16(p);
      p += 2;
      break;
    case LIST_EXT:
      mode = *p++;
      list_length = read_uint32(p);
      p += 4;
    break;
    default:
      fprintf(stderr, "Expected LIST_EXT (%d) or STRING_EXT (%d) got %d\n\r",
        STRING_EXT, LIST_EXT, *p);
      return -1;
  }
  
  for (uint32_t i = 0; i < list_length; i++) {
    if (mode == STRING_EXT) {
      array[i] = *p++;
    } else if (mode == LIST_EXT) {
      switch (*p++) {
        case SMALL_INTEGER_EXT:
          array[i] = *p++;
          break;
        case INTEGER_EXT:
          array[i] = read_int32(p);
          p += 4;
          break;
        case BINARY_EXT:
          {
          }
        default:
          fprintf(stderr, "Unexpected EXT (%d)\n\r", *(p-1));
          return -1;
      }
    } else {
      fprintf(stderr, "Unexpected mode (%d)\n\r", mode);
      return -1;
    }
  }

  if (mode == LIST_EXT) {
    // Check for NIL_EXT to terminate the list
    if (*p != NIL_EXT) {
      fprintf(stderr, "Expected NIL_EXT (%d) got (%d)\n\r", NIL_EXT, *p);
      return -1;
    }
  }

  // Set the array length
  *array_length = list_length;

  return 0;
}

int receiver() {
  uint8_t buffer[1024];
  int array[256];
  int array_length;

  // Read the encoded data from stdin (Elixir port)
  int bytes_read = read(STDIN_FILENO, buffer, sizeof(buffer));
  if (bytes_read < 0) {
    fprintf(stderr, "read error %s\n\r", strerror(errno));
    return -1;
  }

#if 1
  fprintf(stderr, "got %d bytes\n\r", bytes_read);
  int c = 0;
  for (int i=0; i<bytes_read; i++) {
    fprintf(stderr, "%3d ", buffer[i]);
    if (i && i % 10 == 0) fprintf(stderr, "\n\r");
  }
  fprintf(stderr, "\n\r");
#endif

  // Decode the ETF data
  if (decode_int_array(buffer, array, &array_length) == 0) {
    // Print the decoded integers
    fprintf(stderr, "Decoded integers:\n\r");
    for (int i = 0; i < array_length; i++) {
      fprintf(stderr, "[%d] %d\n\r", i, array[i]);
    }
  } else {
    fprintf(stderr, "Didn't get 0\n\r");
  }

  fflush(stdout);
  fflush(stderr);

  return 0;
}

int main() {
  while (1) {
    fprintf(stderr, "receiver()\n\r");
    if (receiver() < 0) {
      break;
    }
  }
  return 0;
}
