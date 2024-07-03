#include <endian.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define EXA_LOG_ALL (0)
#define EXA_LOG_FATAL (1)
#define EXA_LOG_ERROR (2)
#define EXA_LOG_DEBUG (2)
#define EXA_LOG_INFO (4)

static int _exa_log_level = 0;

// it's only on or off for now... the defines above signal intent though...
#define LOG(...) if (_exa_log_level) fprintf(stderr, __VA_ARGS__)

// Erlang terms we may parse or emit

#define ETF_MAGIC         (131)
#define SMALL_INTEGER_EXT (97)
#define INTEGER_EXT       (98)
#define SMALL_TUPLE_EXT   (104)
#define NIL_EXT           (106)
#define STRING_EXT        (107) // followed by two bytes of length (big-endian)
#define LIST_EXT          (108) // followed by four bytes of length
#define BINARY_EXT        (109) // followed by four bytes of length

#define ZLIB_EXT          (80)  // followed by four bytes of uncompressed length and a zlib stream...

/*

# ----

iex(2)> :erlang.term_to_binary([1,2,3], [:compressed])
<<131, 107, 0, 3, 1, 2, 3>>

iex(3)> a = :erlang.term_to_binary([1,2,3,5,6,7,8,9,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0],[:compressed])
<<131, 80, 0, 0, 0, 27, 120, 156, 203, 102, 144, 96, 100, 98, 102, 101, 99, 231,
  224, 100, 64, 3, 0, 16, 211, 0, 173>>

iex(6)> b = :erlang.binary_to_term a
[1, 2, 3, 5, 6, 7, 8, 9, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0]

iex(7)> length b
24

iex(9)> z = :erlang.binary_to_list a
[131, 80, 0, 0, 0, 27, 120, 156, 203, 102, 144, 96, 100, 98, 102, 101, 99, 231,
224, 100, 64, 3, 0, 16, 211, 0, 173]

iex(10)> length z
27

# -----
*/


#define KEY_STORE (33)
#define KEY_MAX (KEY_STORE-1)

enum {
  exa_nil,
  exa_int, // stored in val
  exa_list, // stored in len/list
  exa_binary // stored in blob/list
} exa_type;

struct exa_tuple {
  uint8_t type;
  int32_t val;
  char key[KEY_STORE];
  uint8_t count;
  int32_t number;
  uint8_t *blob;
  int32_t *list;
  int len;
  int error;
};

#define CR "\n\r"

void exa_dump(struct exa_tuple *tuple) {
  if (!tuple) return;
  char *s = "";
  LOG("{");
  if (tuple->key) LOG("\"%s\"", tuple->key);
  switch (tuple->type) {
    case exa_nil:
      break;
    case exa_int:
      LOG(",%d", tuple->val);
      break;
    case exa_list:
      LOG(",[");
      for (int i=0; i<tuple->len; i++) {
        LOG("%s%d", s, tuple->list[i]);
        s = ",";
      }
      LOG("]");
      break;
    case exa_binary:
      LOG(",\"%s\"", tuple->blob);
      break;
    default:
      LOG("?"CR);
      break;
  }
  LOG("}"CR);
}

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

enum {
  read_okay = 100,
  read_empty,
  read_error,
  
  read_fd_negative,
  read_null_struct,

  read_bad_magic,
  read_not_tuple,
  read_bad_tuple_len,
  read_bad_ext,
  read_bad_val,

  read_bad_len,
  read_unknown,

} read_errors;

static int _read_real_error = 0;

int read_real_error(void) {
  return _read_real_error;
}

int readb4(int fd, uint32_t *four) {
  uint32_t n = 0;
  int r = read(fd, &n, sizeof(uint32_t));
  _read_real_error = r;
  if (r < 0) return -read_error;
  if (r == 0) return -read_empty;
  if (r != sizeof(uint32_t)) return -read_bad_len;
  n = be32toh(n);
  if (four) *four = n;
  return read_okay;
}

int readb2(int fd, uint16_t *two) {
  uint16_t n;
  int r = read(fd, &n, sizeof(uint16_t));
  _read_real_error = r;
  if (r < 0) return -read_error;
  if (r == 0) return -read_empty;
  if (r != sizeof(uint16_t)) return -read_bad_len;
  if (two) *two = be16toh(n);
  return read_okay;
}

int readb1(int fd, uint8_t *one) {
  uint8_t n = 0;
  int r = read(fd, &n, sizeof(uint8_t));
  _read_real_error = r;
  if (r < 0) return -read_error;
  if (r == 0) return -read_empty;
  if (r != sizeof(uint8_t)) return -read_bad_len;
  if (one) *one = n;
  return read_okay;
}

int readpast(int fd, size_t skip) {
  uint8_t ignore;
  for (int i=0; i<skip; i++) {
    int r = read(fd, &ignore, sizeof(ignore));
    _read_real_error = r;
    if (r < 0) return -read_error;
    if (r == 0) return -read_empty;
    if (r != skip) return -read_bad_len;
  }
  return read_okay;
}

int free_list_fail(struct exa_tuple *tuple, int r) {
  if (tuple && tuple->list) {
    free(tuple->list);
    tuple->list = NULL;
    tuple->len = 0;
  }
  return r;
}

enum {
  write_okay = 100,
} write_errors;

int exa_send(int fd, struct exa_tuple *tuple) {
  return write_okay;
}

int exa_parse(int fd, struct exa_tuple *tuple) {
  if (fd < 0) return -read_fd_negative;
  if (!tuple) return -read_null_struct;
 
  tuple->key[0] = '\0';
  tuple->type = exa_nil;
  tuple->count = 0;
  if (tuple->list) free(tuple->list);
  tuple->list = NULL;
  if (tuple->blob) free(tuple->blob);
  tuple->blob = NULL;
  tuple->len = 0;

  uint8_t one;
  uint32_t four;
  uint16_t two;

  int r;
  
  // match magic
  if (readb1(fd, &one) < 0 || one != ETF_MAGIC) return -read_bad_magic;
  // match small tuple
  if (readb1(fd, &one) < 0 || one != SMALL_TUPLE_EXT) return -read_not_tuple;
  // get tuple len
  if (readb1(fd, &one) < 0 || one > 2) return -read_bad_tuple_len;
  tuple->count = one;
  if (tuple->count == 0) return read_okay;
  // match bin
  if (readb1(fd, &one) < 0 || one != BINARY_EXT) return -read_bad_ext;
  // match 4 byte len
  if (readb4(fd, &four) < 0) return -read_bad_len;
  uint32_t len = four;
  uint32_t skip = 0;
  if (len > KEY_MAX) {
    skip = len - KEY_MAX;
    len = KEY_MAX;
  }
  if (read(fd, tuple->key, len) < len) return -read_bad_len;
  if (skip && readpast(fd, skip) < 0) return -read_bad_len;

  // make sure key is null-terminated
  tuple->key[len] = '\0';

  if (tuple->count == 1) return read_okay;

  // next we look for...
  //
  // NIL_EXT = no list follows
  // SMALL_INTEGER_EXT = one uint8_t
  // INTEGER_EXT = one int32_t
  // BINARY_EXT = binary blob of uint8_t
  // STRING_EXT = simple list up to 65535 uint8_t
  // LIST_EXT = list of either uint8_t or int32_t
  
  if (readb1(fd, &one) < 0) return -read_bad_ext;

  switch (one) {
    case NIL_EXT:
      tuple->type = exa_nil;
      return read_okay;
    case SMALL_INTEGER_EXT:
      if (readb1(fd, &one) < 0) return -read_bad_len;
      tuple->type = exa_int;
      tuple->val = one;
      return read_okay;
    case INTEGER_EXT:
      if (readb4(fd, &four) < 0) return -read_bad_len;
      tuple->type = exa_int;
      tuple->val = four;
      return read_okay;
    case BINARY_EXT:
      if (readb4(fd, &four) < 0) return -read_bad_len;
      len = four;
      if (len) {
        tuple->blob = (uint8_t *)malloc((len+1) * sizeof(uint8_t));
        if (tuple->blob) {
          if (read(fd, tuple->blob, len) != len) {
            free(tuple->blob);
            tuple->blob = NULL;
            return -read_bad_len;
          }
          tuple->type = exa_binary;
          tuple->len = len;
          tuple->blob[len] = '\0';
          return read_okay;
        }
      }
      break;
    case STRING_EXT:
      if (readb2(fd, &two) < 0) return -read_bad_len;
      len = two;
      if (len) {
        tuple->list = (int32_t *)malloc(len * sizeof(int32_t));
        if (tuple->list) {
          for (int i=0; i<len; i++) {
            if (readb1(fd, &one) < 0) return free_list_fail(tuple, -read_bad_val);
            tuple->list[i] = one;
          }
          tuple->type = exa_list;
          tuple->len = len;
          return read_okay;
        }
      }
      break;
    case LIST_EXT:
      if (readb4(fd, &four) < 0) return -read_bad_len;
      len = four;
      if (len) {
        tuple->list = (int32_t *)malloc(len * sizeof(int32_t));
        if (tuple->list) {
          for (int i=0; i<len; i++) {
            int32_t n = 0;
            if (readb1(fd, &one) < 0) return free_list_fail(tuple, -read_bad_ext);
            if (one == SMALL_INTEGER_EXT) {
              if (readb1(fd, &one) < 0) return free_list_fail(tuple, -read_bad_val);
              n = one;
            } else if (one == INTEGER_EXT) {
              if (readb4(fd, &four) < 0) return free_list_fail(tuple, -read_bad_val);
              n = four;
            } else {
              LOG("unexpected %d"CR, one);
              return free_list_fail(tuple, -read_bad_ext);
            }
            LOG("[%d] = %d"CR, i, n);
            tuple->list[i] = n;
          }
          if (readb1(fd, &one) < 0) return free_list_fail(tuple, -read_bad_ext);
          if (one != NIL_EXT) return free_list_fail(tuple, -read_bad_ext);
          tuple->type = exa_list;
          tuple->len = len;
          return read_okay;
        }
      }
      break;
    default:
      LOG("unexpected %d"CR, one);
      return -read_bad_ext;
  }
  return -read_unknown;
}

void cleaner(void) {
  LOG("cleaner()"CR);
  fflush(stdout);
  fflush(stderr);
}

// -----------------------------------------------------------

// MA stuff

#define MA_NO_FLAC
#define MA_NO_MP3
#define MA_NO_RESOURCE_MANAGER
#define MA_NO_NODE_GRAPH
#define MA_NO_ENGINE
#define MA_NO_GENERATION
//#define MA_DEBUG_OUTPUT
#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

#define SAMPLERATE (44100)
#define CHANNELS (2)

// basic attempt to get a 12-bit value from a device name
// to use as a semi-predictable ID that is
// or-ed with 0x1000 for outputs or 0x2000

#define H12SEED (0x0c1)
int hash12(char *s) {
  int acc = H12SEED;
  if (!s) return 0;
  uint8_t *b = s;
  uint8_t n = 0;
  while (*b) {
    // i added the positional value (n) xor-ed with char
    acc = (acc * 31 + ((*b)^(n))) & 0xfff;
    b++;
    n++;
  }
  return acc;
}

#define OUTPUT_HASH_MASK (0x1000)
#define INPUT_HASH_MASK  (0x2000)

#define DEVICE_NAME_SIZE (160)

#include "uthash.h"

static int ctx_count = 0;

struct s_context {
  int id;
  int visited;
  ma_context *ctx;
  void (*data_cb)(ma_device*, void*, const void*, ma_uint32);
  void (*notify_cb)(const ma_device_notification*);
  UT_hash_handle hh;
} *contexts = NULL;

struct s_device {
  int ctxid;
  ma_device *dev;
  ma_device_id *devid;
  int devindex;
  char attached;
  char visited;
  int id;
  char name[DEVICE_NAME_SIZE];
  char isDefault;
  UT_hash_handle hh;
} *devices = NULL;

void devinfo(void) {
  struct s_device *s;
  for (s = devices; s != NULL; s = s->hh.next) {
    char *type = "INPUT";
    if (s->id & OUTPUT_HASH_MASK) type = "OUTPUT";
    char *attached = "DETACHED";
    if (s->attached) attached = "ATTACHED";
    char *isdefault = "";
    if (s->isDefault) isdefault = "DEFAULT";
    LOG("%d,<<%s>> %s %s %s [%d]"CR,
      s->id, s->name,
      type, attached, isdefault, s->ctxid);
  }
}

struct s_device *find_device(int id) {
  struct s_device *s;
  HASH_FIND_INT(devices, &id, s);
  return s;
}

ma_device_info *out_info;
ma_uint32 out_count;
ma_device_info *in_info;
ma_uint32 in_count;

#define DEV_INFO_OUT (0)
#define DEV_INFO_IN (1)
#define DEV_INFO_COUNT (2)

struct {
  ma_device_info *info;
  ma_uint32 count;
  int hash_mask;
} top[DEV_INFO_COUNT];

void scan_devices(void) {
  static char first = 1;
  if (first) {
    top[DEV_INFO_OUT].hash_mask = OUTPUT_HASH_MASK;
    top[DEV_INFO_IN].hash_mask = INPUT_HASH_MASK;
    first = 0;
  }

  ma_context *ctx = (ma_context *)malloc(sizeof(ma_context));
  
  LOG("=> scan_devices"CR);
  LOG("miniaudio version %s"CR, ma_version_string());

  LOG("=> ma_context_init"CR);
  if (ma_context_init(NULL, 0, NULL, ctx) != MA_SUCCESS) {
    LOG("failed to get ma_context"CR);
    return;
  }

  if (ma_context_get_devices(ctx,
    &top[DEV_INFO_OUT].info, &top[DEV_INFO_OUT].count,
    &top[DEV_INFO_IN].info, &top[DEV_INFO_IN].count) != MA_SUCCESS) {
    LOG("failed to get I/O device list"CR);
    goto clean;
  }

  int new_or_reattached_devices = 0;

  // clear visited flag so we can detect if a device was removed
  struct s_device *s;
  for (s = devices; s != NULL; s = s->hh.next) {
    s->visited = 0;
  }
  // scan and update, adding as necessary
  for (int which = 0; which < DEV_INFO_COUNT; which++) {
    int hash_mask = top[which].hash_mask;
    int count = top[which].count;
    for (int i=0; i<count; i++) {
      ma_device_info *info = &top[which].info[i];
      char *name = info->name;
      int h12 = hash12(name) | hash_mask;
      struct s_device *s = find_device(h12);
      if (!s) {
        s = malloc(sizeof *s);
        s->id = h12;
        s->ctxid = ctx_count;
        LOG("attach %d"CR, h12);
        strcpy(s->name, name);
        new_or_reattached_devices++;
        HASH_ADD_INT(devices, id, s);
      } else if (!s->attached) {
        LOG("reattach %d"CR, h12);
        s->ctxid = ctx_count;
        new_or_reattached_devices++;
      }
      s->visited = 1;
      s->devid = &info->id;
      s->attached = 1;
      s->isDefault = info->isDefault;
    }
    // clear visited flag we can tell if a device was removed
    for (s = devices; s != NULL; s = s->hh.next) {
      // make sure we match the device type (in vs out)
      // by masking with the hash_mask for this info[] table
      if ((s->id & hash_mask) && !s->visited) {
        LOG("detach %d"CR, s->id);
        s->attached = 0;
        // this is an opportunity to mark the context table
        // in some way that it makes it easier to remove
        // unused context entries
        s->ctxid = -1;
      }
    }
  }

  devinfo();
  clean:

  if (new_or_reattached_devices) {
    // create new entry in context table
    struct s_context *c = NULL;
    c = malloc(sizeof *c);
    c->id = ctx_count;
    c->ctx = ctx;
    c->data_cb = NULL;
    c->notify_cb = NULL;
    int id = ctx_count;
    HASH_ADD_INT(contexts, id, c);
    ctx_count++;
  } else {
    LOG("=> ma_context_uninit"CR);
    ma_context_uninit(ctx);
  }
}

//

void data_cb(ma_device *pDevice, void *pOutput, const void *pInput, ma_uint32 frame_count) {
  if (pDevice) {
    if (pOutput) {
    }
    if (pInput) {
    }
  }
}

//

void notification_cb(const ma_device_notification *pNotification) {
  if (pNotification) {
    switch (pNotification->type) {
      case ma_device_notification_type_started:
        break;
      case ma_device_notification_type_stopped:
        break;
      case ma_device_notification_type_rerouted:
        break;
      case ma_device_notification_type_interruption_began:
        break;
      case ma_device_notification_type_interruption_ended:
        break;
      case ma_device_notification_type_unlocked:
        break;
      default:
        break;
    }
  } else {
  }
}

//

int writeb1(int fd, uint8_t c) {
  return write(fd, &c, sizeof(uint8_t));
}

//

int main(int argc, char *argv[]) {
  atexit(cleaner);

  struct exa_tuple tuple;

  tuple.blob = NULL;
  tuple.list = NULL;
  tuple.type = exa_nil;
  tuple.len = 0;
  tuple.count = 0;

  int fd = STDIN_FILENO;

  pid_t parent = getppid();

  // if elixir dies, our parent changes... so quit
  // if elixir closes us, reads from stdin return 0... so quit

  while (1) {
    if (parent != getppid()) {
      LOG("parent changed!"CR);
      break;
    }
    if (exa_parse(fd, &tuple) == read_okay) {
      if (strcmp(tuple.key, "scan") == 0) {
        LOG("scan"CR);
        scan_devices();
        writeb1(STDOUT_FILENO, 131);
        writeb1(STDOUT_FILENO, 104);
        writeb1(STDOUT_FILENO, 0);
        /* should return something like
           [
            {"name0", id0, [:capability_atoms]},
            {"name1", id1, [:capability_atoms]},
            ...
           ]
        */
        // init_devices();
        // list_devices();
        // uninit_devices();
        // should return {"devices",[0,1,2]}
      } else if (strcmp(tuple.key, "use") == 0) {
        LOG("use"CR);
        // without a value, duplex with default input/output
        // otherwise, expects two values, input hash/output hash
        // CREATE a "active" table to hold the context/config/device
      } else if (strcmp(tuple.key, "record") == 0) {
        LOG("record"CR);
        // expects a sample count, returns the array after it's captured
      } else if (strcmp(tuple.key, "play") == 0) {
        LOG("play"CR);
        // expects a sample array
        // more command ideas
        // 44100 16bit signed 1 channel
        // store-0 [] - stores inside exaudio
        // play-0 one-shot
        // loop-0 forever
        // stop-0
        // record-0 frames - gets # of frames to buffer inside exaudio
        // get-0 -> sends exaudio frames to elixir
        // 8 slots : 0-7
      } else if (strcmp(tuple.key, "log-on") == 0) {
        _exa_log_level = 1;
      } else if (strcmp(tuple.key, "log-off") == 0) {
        _exa_log_level = 0;
      } else {
        exa_dump(&tuple);
      }
    } else if (read_real_error() <= 0) {
      LOG("read error <%s>"CR, strerror(errno));
      break;
    }
  }

  LOG("exit receive loop"CR);

  if (tuple.blob) free(tuple.blob);
  if (tuple.list) free(tuple.list);

  struct s_device *cur_dev, *tmp_dev;
    HASH_ITER(hh, devices, cur_dev, tmp_dev) {
    LOG("remove device %d"CR, cur_dev->id);
    HASH_DEL(devices, cur_dev);
    free(cur_dev);
  }

  struct s_context *cur_ctx, *tmp_ctx;
    HASH_ITER(hh, contexts, cur_ctx, tmp_ctx) {
    LOG("remove context %d"CR, cur_ctx->id);
    HASH_DEL(contexts, cur_ctx);
    LOG("=> ma_context_uninit %p"CR, cur_ctx->ctx);
    ma_context_uninit(cur_ctx->ctx);
    free(cur_ctx);
  }

  return 0;
}
