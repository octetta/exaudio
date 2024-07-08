#include <endian.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

#define FD_PRINTF_MAX (1024)

static char _fd_printf_buf[FD_PRINTF_MAX];

int fd_printf(int fd, const char * fmt, ...) {
  int n;
  va_list ap;
  va_start (ap, fmt);
  n = vsnprintf (_fd_printf_buf, FD_PRINTF_MAX, fmt, ap);
  va_end (ap);
  write (fd, _fd_printf_buf, n);
  return n;
}

#define EXA_LOG_ALL (0)
#define EXA_LOG_FATAL (1)
#define EXA_LOG_ERROR (2)
#define EXA_LOG_DEBUG (2)
#define EXA_LOG_INFO (4)

static int _exa_log_level = 1;

// it's only on or off for now... the defines above signal intent though...
#define LOG(...) if (_exa_log_level) fd_printf(STDERR_FILENO, __VA_ARGS__)

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

080 000 000 000 027 120 156 203 102 144 096 100 098 102 101 099 231 224 100 064 003 000 016 211 000 173
ZLB L3  L2  L1  L0  78  9C
    27 bytes------- ZMAGIC
    make a buffer to decompress to that's 27 bytes

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
131 104 000

{"info"}
131 104 001 109 000 000 000 004 105 110 102 111

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
  read_eof,
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

int readbn(int fd, void *m, size_t len) {
  int origlen = len;
  if (fd < 0) {
    // LOG("fd=%d"CR, fd);
    return -read_fd_negative;
  }
  uint8_t *c = m;
  int n;
  while (len > 0) {
    n = read(fd, c++, len);
    if (n <= 0) return n;
    len -= n;
  }
  c = m;
  // LOG("readbn(%d,%p,%d) = %d"CR, fd, m, origlen, n);
  // if (n > 0) {
  //   for (int i=0; i<origlen; i++) LOG("%03d ", c[i]);
  //   LOG(CR);
  // }
  return origlen;
}

int readb4(int fd, uint32_t *four) {
  uint32_t n = 0;
  int r = readbn(fd, &n, sizeof(uint32_t));
  if (r <= 0) return r;
  n = be32toh(n);
  if (four) *four = n;
  return sizeof(uint32_t);
}

int readb2(int fd, uint16_t *two) {
  uint16_t n;
  int r = readbn(fd, &n, sizeof(uint16_t));
  if (r <= 0) return r;
  if (two) *two = be16toh(n);
  return sizeof(uint16_t);
}

int readb1(int fd, uint8_t *one) {
  uint8_t n = 0;
  int r = readbn(fd, &n, sizeof(uint8_t));
  if (r <= 0) return r;
  if (one) *one = n;
  return sizeof(uint8_t);
}

int readpast(int fd, size_t skip) {
  uint8_t ignore;
  for (int i=0; i<skip; i++) {
    int r = readb1(fd, &ignore);
    if (r <= 0) return r;
  }
  return skip;
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

/*
  goal to parse
  {}
  {"key"}
  {"key", number}
  {"key", number, number}
  {"key", number, []}
  {"key", number, [0,1,2]}
  {"key", number, [-1,1024]}

  examples
  {"capture"}
  {"capture", devid}
  {"buffer", size}
  {"record", devid, bufid}
  {"play", devid, bufid}
  {"store", bufid, [0,1,2]}
  {"store", bufid, [-1000,1000]}
*/

int exa_parse(int fd, struct exa_tuple *tuple) {
  LOG("exa_parse"CR);
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
  
  int n;
  // match magic
  if ((n = readb1(fd, &one)) <= 0) return n;
  if (one != ETF_MAGIC) return -read_bad_magic;
  // match small tuple
  if ((n = readb1(fd, &one)) <= 0) return n;
  if (one != SMALL_TUPLE_EXT) return -read_not_tuple;
  // get tuple len
  if ((n = readb1(fd, &one)) <= 0) return n;
  if (one > 2) return -read_bad_tuple_len;
  tuple->count = one;
  LOG("tuple->count = %d"CR, tuple->count);
  if (tuple->count == 0) return read_okay;
  if (one > 2) return -read_bad_tuple_len;
  // match bin
  if ((n = readb1(fd, &one)) <= 0) return n;
  if (one != BINARY_EXT) return -read_bad_ext;
  // match 4 byte len
  if ((n = readb4(fd, &four)) <= 0) return n;
  uint32_t len = four;
  if (len > KEY_MAX) return -read_bad_len;
  if ((n = readbn(fd, tuple->key, len)) <= 0) return n;
  if (n != len) return -read_bad_len;

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
  
  if ((n = readb1(fd, &one)) <= 0) return n;

  switch (one) {
    case NIL_EXT:
      tuple->type = exa_nil;
      return read_okay;
    case SMALL_INTEGER_EXT:
      if ((n = readb1(fd, &one)) <= 0) return n;
      tuple->type = exa_int;
      tuple->val = one;
      return read_okay;
    case INTEGER_EXT:
      if ((n = readb4(fd, &four)) <= 0) return n;
      tuple->type = exa_int;
      tuple->val = four;
      return read_okay;
    case BINARY_EXT:
      if ((n = readb4(fd, &four)) <= 0) return n;
      len = four;
      if (len) {
        tuple->blob = (uint8_t *)malloc((len+1) * sizeof(uint8_t));
        if (tuple->blob) {
          n = readbn(fd, tuple->blob, len);
          if (n <= 0) {
            free(tuple->blob);
            tuple->blob = NULL;
            return n;
          }
          if (n != len) {
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
      if ((n = readb2(fd, &two)) <= 0) return n;
      len = two;
      if (len) {
        tuple->list = (int32_t *)malloc(len * sizeof(int32_t));
        if (tuple->list) {
          for (int i=0; i<len; i++) {
            if ((n = readb1(fd, &one)) <= 0) {
              free_list_fail(tuple, -read_bad_val);
              return n;
            }
            if ((n = readb1(fd, &one)) <= 0) {
              free_list_fail(tuple, -read_bad_val);
              return n;
            }
            tuple->list[i] = one;
          }
          tuple->type = exa_list;
          tuple->len = len;
          return read_okay;
        }
      }
      break;
    case LIST_EXT:
      if ((n = readb4(fd, &four)) <= 0) return n;
      len = four;
      if (len) {
        tuple->list = (int32_t *)malloc(len * sizeof(int32_t));
        if (tuple->list) {
          for (int i=0; i<len; i++) {
            int32_t num = 0;
            // get type
            if ((n = readb1(fd, &one)) <= 0) {
              free_list_fail(tuple, -read_bad_ext);
              return n;
            }
            // take next step based on type
            if (one == SMALL_INTEGER_EXT) {
              if ((n = readb1(fd, &one)) <= 0) {
                free_list_fail(tuple, -read_bad_val);
                return n;
              }
              num = one;
            } else if (one == INTEGER_EXT) {
              if (readb4(fd, &four) <= 0) {
                free_list_fail(tuple, -read_bad_val);
                return n;
              }
              num = four;
            } else {
              LOG("unexpected %d"CR, one);
              free_list_fail(tuple, -read_bad_ext);
              return -read_bad_ext;
            }
            LOG("[%d] = %d"CR, i, num);
            tuple->list[i] = num;
          }
          // get end of list nil
          if ((n = readb1(fd, &one)) <= 0) {
            free_list_fail(tuple, -read_bad_ext);
            return n;
          }
          if (one != NIL_EXT) {
            free_list_fail(tuple, -read_bad_ext);
            return -read_bad_ext;
          }
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

// in a future version, these should be changeable

#define SAMPLERATE (44100)
#define CHANNELS (1)
#define FORMAT ma_format_s16
#define PERIOD_IN_FRAMES (4096) // 1024 sounds BAD, as the callback can't keep up

// basic attempt to get a 12-bit value from a device name
// to use as a semi-predictable ID that is
// or-ed with 0x1000 for playback or 0x2000 for capture

#define H12SEED (0x0c1)
int hash12(char *s, int mask) {
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
  return acc | mask;
}

#define TYPE_PLAYBACK (0x1000)
#define TYPE_CAPTURE  (0x2000)

#define DEVICE_NAME_SIZE (160)

#include "uthash.h"

static int ctx_id_counter = 0;

static struct s_context {
  int id;
  int refs;
  ma_context *ctx;
  UT_hash_handle hh;
} *contexts = NULL;

/*

contexts point to devices

devices point to configs

configs are where the audio comes in and goes out

the scan function populates the contexts
and devices structures

the api user then chooses which capture and playback
which populates the configs structure

*/

struct s_context *find_context(int id) {
  struct s_context *ctx;
  HASH_FIND_INT(contexts, &id, ctx);
  return ctx;
}

enum {
  audio_state_idle = 0,
  audio_state_go,
  audio_state_running,
  audio_state_done,
};

struct s_audio {
  uint32_t len; // allocated size
  int16_t *buffer;
  char state;
  uint32_t position; // position used by the callback
};

int16_t capture_buffer_1[SAMPLERATE * CHANNELS];
uint32_t capture_len_1 = SAMPLERATE;
int16_t playback_buffer_1[SAMPLERATE * CHANNELS];
uint32_t playback_len_1 = SAMPLERATE;

struct s_audio playback_audio = {.len = SAMPLERATE, .state = audio_state_idle, .buffer = capture_buffer_1};
struct s_audio capture_audio = {.len = SAMPLERATE, .state = audio_state_idle, .buffer = playback_buffer_1};
struct s_audio capture_audio;

static struct s_device {
  int ctxid;
  int cfgid;
  int type; // distinguish between capture/playback
  // ma_device *dev;
  // ma_device_id *devid;
  // int devindex;
  //
  char assigned;
  ma_device dev;
  ma_device_config cfg;
  //
  char attached;
  char visited;
  uint64_t data_cb_count;
  int id;
  char name[DEVICE_NAME_SIZE];
  char isDefault;
  struct s_audio *audio;
  UT_hash_handle hh;
} *devices = NULL;

void devinfo(void) {
  struct s_device *dev;
  for (dev = devices; dev != NULL; dev = dev->hh.next) {
    char *type = "CAPTURE";
    if (dev->id & TYPE_PLAYBACK) type = "PLAYBACK";
    char *attached = "DETACHED";
    if (dev->attached) attached = "ATTACHED";
    char *isdefault = "";
    if (dev->isDefault) isdefault = "DEFAULT";
    char *assigned = "";
    if (dev->assigned) assigned = "ASSIGNED";
    LOG("%d,<<%s>> %04x %s %s %s [%d/%d] %d %s %p"CR,
      dev->id, dev->name, dev->type,
      type, attached, isdefault,
      dev->ctxid, dev->cfgid,
      dev->data_cb_count, assigned,
      dev->audio);
  }
}

struct s_device *find_device(int id) {
  struct s_device *dev;
  HASH_FIND_INT(devices, &id, dev);
  return dev;
}

#define EXA_INFO_PLAYBACK (0)
#define EXA_INFO_CAPTURE (1)
#define EXA_INFO_COUNT (2)

struct {
  ma_device_info *info;
  ma_uint32 count;
  int type;
  int defaultid;
} exa_info[EXA_INFO_COUNT];

void scan(void) {
  static char first = 1;
  if (first) {
    exa_info[EXA_INFO_PLAYBACK].type = TYPE_PLAYBACK;
    exa_info[EXA_INFO_CAPTURE].type = TYPE_CAPTURE;
    first = 0;
  }

  ma_context *ctx = (ma_context *)malloc(sizeof(ma_context));
  
  LOG("=> scan"CR);
  LOG("miniaudio version %s"CR, ma_version_string());

  LOG("=> ma_context_init"CR);
  if (ma_context_init(NULL, 0, NULL, ctx) != MA_SUCCESS) {
    LOG("failed to get ma_context"CR);
    return;
  }

  int new_or_reattached_devices = 0;
  int detached_devices = 0;

  if (ma_context_get_devices(ctx,
    &exa_info[EXA_INFO_PLAYBACK].info, &exa_info[EXA_INFO_PLAYBACK].count,
    &exa_info[EXA_INFO_CAPTURE].info, &exa_info[EXA_INFO_CAPTURE].count) != MA_SUCCESS) {
    LOG("failed to get I/O device list"CR);
    goto clean;
  }

  struct s_device *dev;

  // clear visited flag so we can detect if a device was removed
  for (dev = devices; dev != NULL; dev = dev->hh.next) {
    dev->visited = 0;
  }

  // scan and update, adding as necessary
  for (int which = 0; which < EXA_INFO_COUNT; which++) {
    exa_info[which].defaultid = -1; // this should be filled in during the scan
    int type = exa_info[which].type;
    int count = exa_info[which].count;
    for (int i=0; i<count; i++) {
      ma_device_info *info = &exa_info[which].info[i];
      char *name = info->name;
      int h12 = hash12(name, type);
      dev = find_device(h12);
      if (!dev) {
        dev = malloc(sizeof *dev);
        dev->id = h12;
        dev->type = type;
        dev->ctxid = ctx_id_counter;
        dev->cfgid = -1;
        dev->assigned = 0;
        dev->data_cb_count = 0;
        LOG("attach %d"CR, h12);
        strcpy(dev->name, name);
        // hack for audio buffer
        if (type == TYPE_CAPTURE) {
          dev->audio = &capture_audio;
        } else if (type == TYPE_PLAYBACK) {
          dev->audio = &playback_audio;
        }
        //
        new_or_reattached_devices++;
        HASH_ADD_INT(devices, id, dev);
      } else if (!dev->attached) {
        LOG("reattach %d"CR, h12);
        dev->ctxid = ctx_id_counter;
        new_or_reattached_devices++;
      }
      dev->visited = 1;
      dev->attached = 1;
      dev->isDefault = info->isDefault;
      if (dev->isDefault) exa_info[which].defaultid = dev->id;
    }

    // check visited flag we can tell if a device was removed
    for (dev = devices; dev != NULL; dev = dev->hh.next) {
      if ((dev->type == type) && !dev->visited) {
        LOG("detach %d"CR, dev->id);
        dev->attached = 0;
        detached_devices++;
      }
    }
  }

  devinfo();

  clean:

  if (new_or_reattached_devices) {
    // create new entry in context table
    struct s_context *c = NULL;
    c = malloc(sizeof *c);
    c->id = ctx_id_counter;
    c->ctx = ctx;
    int id = ctx_id_counter;
    HASH_ADD_INT(contexts, id, c);
    ctx_id_counter++;
  } else {
    LOG("=> ma_context_uninit"CR);
    ma_context_uninit(ctx);
  }

  // now clean up unreferenced contexts
  if (detached_devices) {
    LOG("DETACHED DEVICES"CR);
    struct s_context *cur_ctx, *tmp_ctx;
    // reset refs
    HASH_ITER(hh, contexts, cur_ctx, tmp_ctx) {
      LOG("CLEAR [%d] refs"CR, cur_ctx->id);
      cur_ctx->refs = 0;
    }
    struct s_device *cur_dev, *tmp_dev;
    // count the number of devices that reference contexts
    HASH_ITER(hh, devices, cur_dev, tmp_dev) {
      //LOG("CHECK dev [%d] ctx [%d]"CR, cur_dev->id, cur_dev->ctxid);
      if (cur_dev->attached && cur_dev->ctxid >= 0) {
        cur_ctx = find_context(cur_dev->ctxid);
        if (cur_ctx) cur_ctx->refs++;
      }
    }
    // remove any unused contexts
    HASH_ITER(hh, contexts, cur_ctx, tmp_ctx) {
      if (cur_ctx->refs == 0) {
        // remove it
        LOG("REMOVE ctx[%d]->refs = %d"CR, cur_ctx->id, cur_ctx->refs);
        HASH_DEL(contexts, cur_ctx);
        ma_context_uninit(cur_ctx->ctx);
        free(cur_ctx); 
      } else {
        LOG("KEEP ctx[%d]->refs = %d"CR, cur_ctx->id, cur_ctx->refs);
      }
    }
    // mark unused contexts in devices
    HASH_ITER(hh, devices, cur_dev, tmp_dev) {
      if (cur_dev->attached == 0) {
        LOG("MARK dev [%d] -1"CR, cur_dev->id);
        cur_dev->ctxid = -1;
      }
    }
  }

  LOG("default playback:%d"CR, exa_info[EXA_INFO_PLAYBACK].defaultid);
  LOG("default  capture:%d"CR, exa_info[EXA_INFO_CAPTURE].defaultid);
}

//

// this creates a config where the devices are attached to a callback
// where audio is captured and/or played
int use(int capture_id, int playback_id) {
  struct s_device *capture = NULL;
  struct s_device *playback = NULL;
  if (capture_id >= 0) {
    capture = find_device(capture_id);
  }
  if (playback_id >= 0) {
    playback = find_device(playback_id);
  }
  LOG("in:%d/%s out:%d/%s"CR,
    capture_id, capture->name,
    playback_id, playback->name
  );
}

//

uint64_t data_cb_count = 0;
uint64_t data_cb_nodev = 0;
uint64_t data_cb_fail = 0;

void data_cb(ma_device *pDevice, void *playback, const void *capture, ma_uint32 frame_count) {
  if (pDevice) {
    struct s_device *this = (struct s_device *)pDevice->pUserData;
    if (this) {
      this->data_cb_count++;
      if (playback && this->audio && this->audio->buffer && this->audio->state == audio_state_go) {
        this->audio->state = audio_state_running;
        // this give us a chance to trigger something at start
      }
      if (playback && this->audio && this->audio->buffer && this->audio->state == audio_state_running) {
        // copy from audio buffer into device
        int16_t *poke = (int16_t *)playback;
        for(int i=0; i<frame_count; i++) {
          poke[i] = this->audio->buffer[this->audio->position++];
          if (this->audio->position >= this->audio->len) {
            this->audio->state = audio_state_done; // 0 = idle, 1 = go, 2 = inprogress, 3 = done;
            this->audio->position = 0; // reset position
            break;
          }
        }
        /*
        uint32_t n = frame_count * CHANNELS;
        if (this->audio->position + n > this->audio->len) {
          n = this->audio->len - this->audio->position;
        }
        memcpy(playback, this->audio->buffer + this->audio->position, n);
        this->audio->position += n;
        if (n < frame_count * CHANNELS) {
          this->audio->state = audio_state_done; // 0 = idle, 1 = go, 2 = inprogress, 3 = done;
          this->audio->position = 0; // reset position
        }
        */
      }
      if (capture && this->audio && this->audio->buffer && this->audio->state == audio_state_go) {
        this->audio->state = audio_state_running;
        // this give us a chance to trigger something at start
      }
      if (capture && this->audio && this->audio->buffer && this->audio->state == audio_state_running) {
        // copy from device to audio buffer
        int16_t *peek = (int16_t *)capture;
        for(int i=0; i<frame_count; i++) {
          this->audio->buffer[this->audio->position++] = peek[i];
          if (this->audio->position >= this->audio->len) {
            this->audio->state = audio_state_done; // 0 = idle, 1 = go, 2 = inprogress, 3 = done;
            this->audio->position = 0; // reset position
            break;
          }
        }
        /*
        uint32_t n = frame_count * CHANNELS;
        if (this->audio->position + n > this->audio->len) {
          n = this->audio->len - this->audio->position;
        }
        memcpy(this->audio->buffer + this->audio->position, capture, n);
        this->audio->position += n;
        if (n < frame_count * CHANNELS) {
          this->audio->state = audio_state_done; // 0 = idle, 1 = go, 2 = inprogress, 3 = done;
          this->audio->position = 0; // reset position
        }
        */
      }
    } else {
      data_cb_fail++;
    }
    data_cb_count++;
  } else {
    data_cb_nodev++;
  }
}

//

void notification_cb(const ma_device_notification *pNotification) {
  if (pNotification) {
    switch (pNotification->type) {
      case ma_device_notification_type_started:
        LOG("notify started %p"CR, pNotification->pDevice);
        break;
      case ma_device_notification_type_stopped:
        LOG("notify stopped %p"CR, pNotification->pDevice);
        break;
      case ma_device_notification_type_rerouted:
        LOG("notify rerouted %p"CR, pNotification->pDevice);
        break;
      case ma_device_notification_type_interruption_began:
        LOG("notify interrupt began %p"CR, pNotification->pDevice);
        break;
      case ma_device_notification_type_interruption_ended:
        LOG("notify interrupt ended %p"CR, pNotification->pDevice);
        break;
      case ma_device_notification_type_unlocked:
        LOG("notify unlocked %p"CR, pNotification->pDevice);
        break;
      default:
        LOG("notify unknown %p"CR, pNotification->pDevice);
        break;
    }
  } else {
  }
}

//

int writeb1(int fd, uint8_t c) {
  return write(fd, &c, sizeof(uint8_t));
}

int writeb4(int fd, uint32_t w) {
  uint32_t n = htobe32(w);
  return write(fd, &n, sizeof(uint32_t));
}

//

struct termios orig_termios;


// maybe id is enough, since the device knows its type
// maybe rename to int use(int id)
int assign(int id, int type) {
  struct s_device *this = find_device(id);
  if (this && this->ctxid >= 0 && this->type == type) {
    if (this->assigned) {
      LOG("uh-oh, a cfg/dev is in this %p"CR, this->dev);
    } else {
      struct s_context *ctx = find_context(this->ctxid);
      if (!ctx) {
        LOG("uh-oh, ctx is null"CR);
      } else {
        // WHAT is the life cycle when things disappear?
        // WHAT needs cleanup?
        if (type == TYPE_CAPTURE) {
          this->cfg = ma_device_config_init(ma_device_type_capture);
          this->cfg.capture.format = FORMAT;
          this->cfg.capture.channels = CHANNELS;
        } else {
          this->cfg = ma_device_config_init(ma_device_type_playback);
          this->cfg.playback.format = FORMAT;
          this->cfg.playback.channels = CHANNELS;
        }
        this->cfg.periodSizeInFrames = PERIOD_IN_FRAMES;
        // this->cfg.periodSizeInMilliseconds = 10;
        this->cfg.sampleRate = 44100;
        this->cfg.dataCallback = data_cb;
        this->cfg.notificationCallback = notification_cb;
        this->cfg.pUserData = this; // should point to something useful, trying struct s_device
        ma_result r = ma_device_init(ctx->ctx, &this->cfg, &this->dev);
        if (r != MA_SUCCESS) {
          LOG("failed to initialize device"CR);
        } else {
          r = ma_device_start(&this->dev);
          if (r != MA_SUCCESS) {
            LOG("failed to start device"CR);
            ma_device_uninit(&this->dev);
          } else {
            this->assigned = 1;
            LOG("OKAY"CR);
          }
        }
      }
    }
  } else {
    LOG("FAIL"CR);
  } 
  return -1;
}

#define SIGN(x) ((x > 0) - (x < 0))

void mkwave(struct s_audio *audio, int wave, float hz, float gain, char find) {
  if (!audio) return;
  if (!audio->buffer) return;
  if (audio->len == 0) return;

  int duration = audio->len;
  float att = gain;
  float acc = gain;

  LOG("att=%g"CR, att);
  LOG("acc=%g"CR, acc);

  float *b = (float *)malloc(duration * sizeof(float));
  float y, gy;

  for (int i = 0; i < duration; i++) {
    y = sin(hz * (2 * M_PI) * i / SAMPLERATE);
    gy = y * att;
    int16_t iy = (int16_t)(gy * 32767);
    b[i] = gy;
    audio->buffer[i] = iy;
    //att -= acc; // attenuate during length of wave
  }
  char found = 0;
  int found_index = 0;
  if (find) {
    // find last sample that crosses zero going positive
    float ly;
    char first = 1;
    for (int i = duration-1; i >= 0; i--) {
      float cy = b[i];
      if (first) {
        ly = cy;
        first = 0;
        continue;
      }
      if (ly > cy) {
        if ((SIGN(ly) > 0) && (SIGN(cy) < 0)) {          
          found = 1;
          found_index = i;
          break;
        }
      }
      ly = b[i];
    }
  }
  if (found) {
    LOG("zero-cross downgoing at [%d]"CR, found_index);
    audio->len = found_index;
  } else {
    LOG("didn't find zero-crossing downgoing"CR);
  }
  LOG("hz:%g gain:%g"CR, hz, gain);
  free(b);
}

int main(int argc, char *argv[]) {
  LOG("exaudio"CR);
  atexit(cleaner);

  mkwave(&playback_audio, 0, 220, 1, 0); // hack to test playback sine wave
  
  struct exa_tuple tuple;

  tuple.blob = NULL;
  tuple.list = NULL;
  tuple.type = exa_nil;
  tuple.len = 0;
  tuple.count = 0;

  // tcgetattr(STDIN_FILENO, &orig_termios);

  // struct termios raw;
  // tcgetattr(STDIN_FILENO, &raw);

  // raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
  // raw.c_oflag &= ~(OPOST);
  // raw.c_cflag |= (CS8);
  // raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
  // raw.c_cc[VMIN] = 0;
  // raw.c_cc[VTIME] = 1;
  // tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);

  int fdin = STDIN_FILENO;
  int fdout = STDOUT_FILENO;

  pid_t parent = getppid();

  // populate devices on startup
  scan();

  // if elixir dies, our parent changes... so quit
  // if elixir closes us, reads from stdin return 0... so quit
  while (1) {
    if (parent != getppid()) {
      LOG("parent changed!"CR);
      break;
    }
    int n = exa_parse(fdin, &tuple);
    if (n == read_okay) {
      LOG("read_okay"CR);
      if (tuple.count == 0) {
        LOG("{}"CR);
      } else if (strcmp(tuple.key, "scan") == 0) {
        LOG("scan"CR);
        scan();
        writeb1(fdout, ETF_MAGIC);
        writeb1(fdout, SMALL_TUPLE_EXT);
        writeb1(fdout, 1); // small tuple length (0 means empty)
        writeb1(fdout, BINARY_EXT);
        char *res = "okay";
        writeb4(fdout, strlen(res));
        write(fdout, res, strlen(res));
        /*
          should return something like
          {"scan", [
            { "name0", id0#, status0#, [ :playback, :detached ] },
            { "name1", id1#, status1#, [ :capture, :attached, :default, :running ] },
          ]}
        */
      } else if (strcmp(tuple.key, "capture") == 0) {
        // maybe {"use", id} is enough?
        // for the {"use"} case, it could setup the default capture and playback?
        if (tuple.count == 1) {
          LOG("capture default"CR);
          assign(exa_info[EXA_INFO_CAPTURE].defaultid, TYPE_CAPTURE);
        } else {
          LOG("capture %d"CR, tuple.val);
          assign(tuple.val, TYPE_CAPTURE);
        }
      } else if (strcmp(tuple.key, "playback") == 0) {
        if (tuple.count == 1) {
          LOG("playback default"CR);
          assign(exa_info[EXA_INFO_PLAYBACK].defaultid, TYPE_PLAYBACK);
        } else {
          LOG("playback %d"CR, tuple.val);
          assign(tuple.val, TYPE_PLAYBACK);
        }
      // } else if (strcmp(tuple.key, "duplex") == 0) {
      //   if (tuple.count == 1) {
      //     LOG("duplex default"CR);
      //   } else {
      //     LOG("duplex %d"CR, tuple.val);
      //   }
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
      } else if (strcmp(tuple.key, "dump") == 0) {
        LOG("data_cb_count:%d"CR, data_cb_count);
        LOG("data_cb_nodev:%d"CR, data_cb_nodev);
        LOG("data_cb_fail:%d"CR, data_cb_fail);
        LOG("capture state:%d"CR, capture_audio.state);
        LOG("playback state:%d"CR, playback_audio.state);
      } else if (strcmp(tuple.key, "start") == 0) {
        // either start capture or playback with buffer assigned to device
        capture_audio.state = audio_state_go;
        playback_audio.state = audio_state_go;
      } else if (strcmp(tuple.key, "exit") == 0) {
        LOG("exit"CR);
        break;
      } else if (strcmp(tuple.key, "log-on") == 0) {
        _exa_log_level = 1;
      } else if (strcmp(tuple.key, "log-off") == 0) {
        _exa_log_level = 0;
      } else {
        exa_dump(&tuple);
      }
    } else if (n <= 0) {
      LOG("read error <%s>"CR, strerror(errno));
      break;
    } else {
      LOG("WAT?"CR);
      exa_dump(&tuple);
    }
    LOG("tuple.list:%p"CR, tuple.list);
    LOG("tuple.blob:%p"CR, tuple.blob);
  }

  LOG("exit receive loop"CR);

  // clean up etf parsing memory
  if (tuple.blob) free(tuple.blob);
  if (tuple.list) free(tuple.list);
  
  // clean up device memory
  struct s_device *cur_dev, *tmp_dev;
    HASH_ITER(hh, devices, cur_dev, tmp_dev) {
    LOG("remove device %d"CR, cur_dev->id);
    HASH_DEL(devices, cur_dev);
    free(cur_dev);
  }

  // clean up context memory
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
