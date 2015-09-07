#include <stdlib.h>
#include <moz/pstring.h>

#include "vm.h"

#ifndef CHAR_BIT
#define CHAR_BIT 8
#endif

size_t malloc_size = 0;
#define VM_MALLOC(N) malloc(N); malloc_size += N;

typedef struct ByteCodeInfo {
  size_t code_length;
  int pos;
  char fileType[4];
  uint8_t version;
  uint16_t instSize;
  uint16_t jmpTableSize;
  uint16_t nonTermPoolSize;
  uint16_t setPoolSize;
  uint16_t strPoolSize;
} ByteCodeInfo;

typedef struct ByteCodeLoader {
  char *input;
  ByteCodeInfo *info;
  MiniNezInstruction *head;
} ByteCodeLoader;

char *loadFile(const char *filename, size_t *length) {
  size_t len = 0;
  FILE *fp = fopen(filename, "rb");
  char *source;
  if (!fp) {
    nez_PrintErrorInfo("fopen error: cannot open file");
    return NULL;
  }
  fseek(fp, 0, SEEK_END);
  len = (size_t)ftell(fp);
  fseek(fp, 0, SEEK_SET);
  source = (char *)malloc(len + 1);
  if (len != fread(source, 1, len, fp)) {
    nez_PrintErrorInfo("fread error: cannot read file collectly");
  }
  source[len] = '\0';
  fclose(fp);
  *length = len;
  return source;
}

static char *peek(char* inputs, ByteCodeInfo *info)
{
    return inputs + info->pos;
}

static void skip(ByteCodeInfo *info, size_t shift)
{
    info->pos += shift;
}

static inline uint8_t read8(char* inputs, ByteCodeInfo *info) {
  return (uint8_t)inputs[info->pos++];
}

static uint16_t read16(char *inputs, ByteCodeInfo *info) {
  uint16_t value = (uint8_t)inputs[info->pos++];
  value = ((value) << 8) | ((uint8_t)inputs[info->pos++]);
  return value;
}

static unsigned read24(char *inputs, ByteCodeInfo *info)
{
    unsigned d1 = read8(inputs, info);
    unsigned d2 = read8(inputs, info);
    unsigned d3 = read8(inputs, info);
    return d1 << 16 | d2 << 8 | d3;
}

static uint32_t read32(char *inputs, ByteCodeInfo *info) {
  uint32_t value = read16(inputs, info);
  value = ((value) << 16) | read16(inputs, info);
  return value;
}

static uint8_t Loader_Read8(ByteCodeLoader *loader) {
  return read8(loader->input, loader->info);
}

static uint16_t Loader_Read16(ByteCodeLoader *loader) {
  return read16(loader->input, loader->info);
}

static unsigned Loader_Read24(ByteCodeLoader *loader) {
  return read24(loader->input, loader->info);
}

static uint32_t Loader_Read32(ByteCodeLoader *loader) {
  return read32(loader->input, loader->info);
}

#if MININEZ_DEBUG == 1

static void dumpByteCodeInfo(ByteCodeInfo *info) {
  fprintf(stderr, "FileType: %s\n", info->fileType);
  fprintf(stderr, "Version: %c\n", info->version);
  fprintf(stderr, "InstSize: %u\n", info->instSize);
  fprintf(stderr, "jmpTableSize: %u\n", info->jmpTableSize);
  fprintf(stderr, "nonTermPoolSize: %u\n", info->nonTermPoolSize);
  fprintf(stderr, "setPoolSize: %u\n", info->setPoolSize);
  fprintf(stderr, "strPoolSize: %u\n", info->strPoolSize);
}


static char *write_char(char *p, unsigned char ch)
{
    switch (ch) {
    case '\\':
        *p++ = '\\';
        break;
    case '\b':
        *p++ = '\\';
        *p++ = 'b';
        break;
    case '\f':
        *p++ = '\\';
        *p++ = 'f';
        break;
    case '\n':
        *p++ = '\\';
        *p++ = 'n';
        break;
    case '\r':
        *p++ = '\\';
        *p++ = 'r';
        break;
    case '\t':
        *p++ = '\\';
        *p++ = 't';
        break;
    default:
        if (32 <= ch && ch <= 126) {
            *p++ = ch;
        }
        else {
            *p++ = '\\';
            *p++ = "0123456789abcdef"[ch / 16];
            *p++ = "0123456789abcdef"[ch % 16];
        }
    }
    return p;
}

static void dump_set(bitset_t *set, char *buf)
{
    unsigned i, j;
    *buf++ = '[';
    for (i = 0; i < 256; i++) {
        if (bitset_get(set, i)) {
            buf = write_char(buf, i);
            for (j = i + 1; j < 256; j++) {
                if (!bitset_get(set, j)) {
                    if (j == i + 1) {}
                    else {
                        *buf++ = '-';
                        buf = write_char(buf, j - 1);
                        i = j - 1;
                    }
                    break;
                }
            }
            if (j == 256) {
                *buf++ = '-';
                buf = write_char(buf, j - 1);
                break;
            }
        }
    }
    *buf++ = ']';
    *buf++ = '\0';
}

#endif

static int loadOpCode(ByteCodeLoader* loader, int* has_jump) {
  uint8_t opcode = Loader_Read8(loader);
  *has_jump = opcode & 0x80;
  opcode = opcode & 0x7f;
  switch (opcode) {
    case 16:
      opcode = MININEZ_OP_Instr;
      break;
    case 20:
      opcode = MININEZ_OP_Iostr;
      break;
    case 21:
      opcode = MININEZ_OP_Ioset;
      break;
    case 25:
      opcode = MININEZ_OP_Irset;
      break;
    case 127:
      opcode = MININEZ_OP_Ilabel;
      break;
  }
  return opcode;
}

void loadMiniNezInstruction(MiniNezInstruction* ir, ByteCodeLoader *loader, Context ctx) {
  unsigned i;
  MiniNezInstruction* head = ir;
  // exit fail case
  ir->op = MININEZ_OP_Iexit;
  ir->arg = 0;
#if MININEZ_DEBUG == 1
  fprintf(stderr, "[0]%s %d\n", get_opname(ir->op), ir->arg);
#endif
  ir++;
  // exit success case
  ir->op = MININEZ_OP_Iexit;
  ir->arg = 1;
#if MININEZ_DEBUG == 1
  fprintf(stderr, "[1]%s %d\n", get_opname(ir->op), ir->arg);
#endif
  ir++;
  int size = loader->info->instSize;
  for(i = 0; i < loader->info->instSize; i++) {
    assert(loader->info->pos < (int)loader->info->code_length);
    int has_jump = 0;
    uint8_t opcode = loadOpCode(loader, &has_jump);
#if MININEZ_DEBUG == 1
    fprintf(stderr, "[%u]%s", i, get_opname(opcode));
#endif
    switch (opcode) {
      case MININEZ_OP_Iexit:
        ir->arg = Loader_Read8(loader);
        break;
      case MININEZ_OP_Icall:
        Loader_Read24(loader) + 2;
        uint16_t nterm = Loader_Read16(loader);
        ir->arg = Loader_Read24(loader) + 2;
        has_jump = 0;
#if MININEZ_DEBUG == 1
        fprintf(stderr, " %u %d", nterm, ir->arg);
#endif
        break;
      case MININEZ_OP_Ialt:
        ir->arg = Loader_Read24(loader) + 2;
#if MININEZ_DEBUG == 1
        fprintf(stderr, " %d", ir->arg);
#endif
        break;
      case MININEZ_OP_Ijump:
        if(has_jump) {
          ir->arg = Loader_Read24(loader) + 2;
        } else {
          ir->arg = ir - head + 1;
        }
        Loader_Read24(loader);
        has_jump = 0;
#if MININEZ_DEBUG == 1
        fprintf(stderr, " %d", ir->arg);
#endif
        break;
      case MININEZ_OP_Iskip:
        ir->arg = Loader_Read24(loader) + 2;
        has_jump = 0;
#if MININEZ_DEBUG == 1
        fprintf(stderr, " %d", ir->arg);
#endif
        break;
      case MININEZ_OP_Ibyte:
      case MININEZ_OP_Inbyte:
        ir->arg = Loader_Read8(loader);
#if MININEZ_DEBUG == 1
        fprintf(stderr, " '%c'", ir->arg);
#endif
        break;
      case MININEZ_OP_Istr:
      case MININEZ_OP_Instr:
      case MININEZ_OP_Iostr:
      case MININEZ_OP_Iset:
      case MININEZ_OP_Ioset:
      case MININEZ_OP_Irset:
        ir->arg = Loader_Read16(loader);
#if MININEZ_DEBUG == 1
        fprintf(stderr, " %u", ir->arg);
#endif
        break;
      case MININEZ_OP_Ilabel: {
        uint16_t label = Loader_Read16(loader);
        ir->arg = label;
        break;
      }
    }
    if (has_jump) {
      int jump = Loader_Read24(loader);
      fprintf(stderr, "\n[%u]error!!", i);
    }
#if MININEZ_DEBUG == 1
    fprintf(stderr, "\n");
#endif
    ir->op = opcode;
    ir++;
  }
}

MiniNezInstruction* loadMachineCode(Context ctx, const char* code_file, const char* start_point) {
  unsigned i;
  MiniNezInstruction* inst = NULL;
  MiniNezInstruction* head = NULL;
  size_t code_length;
  char* buf = loadFile(code_file, &code_length);
  ByteCodeInfo info;
  info.code_length = code_length;
  info.pos = 0;

#if MININEZ_DEBUG == 1
  fprintf(stderr, "Bytecode file size: %zu[byte]\n", code_length);
#endif

  /* load bytecode header */

  /* load file type */
  info.fileType[0] = read8(buf, &info);
  info.fileType[1] = read8(buf, &info);
  info.fileType[2] = read8(buf, &info);
  info.fileType[3] = 0;

  /* load nez version */
  info.version = read8(buf, &info);

  /* load instruction size */
  info.instSize = read16(buf, &info);

  read16(buf, &info); // mininez doesn't use memo size

  /* load jump table size */
  info.jmpTableSize = read16(buf, &info);

  info.nonTermPoolSize = read16(buf, &info);
  if(info.nonTermPoolSize > 0) {
    ctx->nterms = (const char**) malloc(sizeof(const char*) * info.nonTermPoolSize);
    for(i = 0; i < info.nonTermPoolSize; i++) {
      uint16_t len = read16(buf, &info);
      char* str = peek(buf, &info);
      skip(&info, len+1);
      ctx->nterms[i] = pstring_alloc(str, (unsigned)len);
#if MININEZ_DEBUG == 1
      fprintf(stderr, "nterm[%d]: %s\n", i, ctx->nterms[i]);
#endif
    }
  }

  info.setPoolSize = read16(buf, &info);
  if(info.setPoolSize > 0) {
    ctx->sets = (bitset_t*) VM_MALLOC(sizeof(bitset_t) * info.setPoolSize);
#define INT_BIT (sizeof(int) * CHAR_BIT)
#define N (256 / INT_BIT)
    for (i = 0; i < info.setPoolSize; i++) {
      unsigned j, k;
      char debug_buf[512] = {};
      bitset_t *set = &ctx->sets[i];
      bitset_init(set);
      for (j = 0; j < 256/INT_BIT; j++) {
        unsigned v = read32(buf, &info);
        for (k = 0; k < INT_BIT; k++) {
          unsigned mask = 1U << k;
          if ((v & mask) == mask) {
            bitset_set(set, k + INT_BIT * j);
          }
        }
      }
#if MININEZ_DEBUG == 1
      dump_set(set, debug_buf);
      fprintf(stderr, "set: %s\n", debug_buf);
#endif
    }
  }

  info.strPoolSize = read16(buf, &info);
  if(info.strPoolSize > 0) {
    ctx->strs = (const char **) VM_MALLOC(sizeof(const char *) * info.strPoolSize);
    for (i = 0; i < info.strPoolSize; i++) {
      uint16_t len = read16(buf, &info);
      char *str = peek(buf, &info);
      skip(&info, len + 1);
      ctx->strs[i] = pstring_alloc(str, (unsigned)len);
      malloc_size += sizeof(pstring_t) + len + 1;
#if MININEZ_DEBUG == 1
      fprintf(stderr, "str[%d]: '%s'\n", i, ctx->strs[i]);
#endif
    }
  }

#if MININEZ_DEBUG == 1
  dumpByteCodeInfo(&info);
#endif

  read16(buf, &info); // mininez doesn't use tag
  read16(buf, &info); // mininez doesn't use symbol table

  /*
  ** head is a tmporary variable that indecates the begining
  ** of the instruction sequence
  */
  head = inst = VM_MALLOC(sizeof(*inst) * (info.instSize + 2));
  memset(inst, 0, sizeof(*inst) * (info.instSize + 2));

  /* init bytecode loader */
  ByteCodeLoader *loader = malloc(sizeof(ByteCodeLoader));
  loader->input = buf;
  loader->info = &info;
  loader->head = head;

  loadMiniNezInstruction(head, loader, ctx);

  fprintf(stderr, "byte code memory: %zu [byte]\n", malloc_size);

  return head;
}
