#include <stdlib.h>
#include <moz/pstring.h>

#include "vm.h"

typedef struct ByteCodeInfo {
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

static uint32_t Loader_Read32(ByteCodeLoader *loader) {
  return read32(loader->input, loader->info);
}

static void dumpByteCodeInfo(ByteCodeInfo *info) {
  fprintf(stderr, "FileType: %s\n", info->fileType);
  fprintf(stderr, "Version: %c\n", info->version);
  fprintf(stderr, "InstSize: %u\n", info->instSize);
  fprintf(stderr, "jmpTableSize: %u\n", info->jmpTableSize);
  fprintf(stderr, "nonTermPoolSize: %u\n", info->nonTermPoolSize);
  fprintf(stderr, "setPoolSize: %u\n", info->setPoolSize);
  fprintf(stderr, "strPoolSize: %u\n", info->strPoolSize);
}

void loadMiniNezInstruction(MiniNezInstruction* ir, ByteCodeLoader *loader, Context ctx) {
  unsigned i;
  // exit fail case
  ir->op = MININEZ_OP_Iexit;
  ir->arg = 0;
  fprintf(stderr, "[0]%s %d\n", get_opname(ir->op), ir->arg);
  ir++;
  // exit success case
  ir->op = MININEZ_OP_Iexit;
  ir->arg = 1;
  fprintf(stderr, "[1]%s %d\n", get_opname(ir->op), ir->arg);
  ir++;
  for(i = 2; i < loader->info->instSize; i++) {
    uint8_t opcode = Loader_Read8(loader);
    fprintf(stderr, "[%u]%s", i, get_opname(opcode));
    switch (opcode) {
      case MININEZ_OP_Ibyte:
        ir->arg = Loader_Read8(loader);
        fprintf(stderr, " '%c'", ir->arg);
        break;
      case 127:
        opcode = MININEZ_OP_Ilabel;
        uint16_t label = Loader_Read16(loader);
        ir->arg = label;
        break;
    }
    fprintf(stderr, "\n");
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
  info.pos = 0;

  fprintf(stderr, "Bytecode file size: %zu[byte]\n", code_length);
  /* load bytecode header */

  /* load file type */
  info.fileType[0] = read8(buf, &info);
  info.fileType[1] = read8(buf, &info);
  info.fileType[2] = read8(buf, &info);
  info.fileType[3] = 0;

  /* load nez version */
  info.version = read8(buf, &info);

  /* load instruction size */
  info.instSize = read16(buf, &info) + 2;

  assert(read16(buf, &info) == 0); // mininez doesn't use memo size

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
      fprintf(stderr, "nterm[%d]: %s\n", i, ctx->nterms[i]);
    }
  }

  info.setPoolSize = read16(buf, &info);
  if(info.setPoolSize > 0) {
    // TODO
  }

  info.strPoolSize = read16(buf, &info);
  if(info.strPoolSize > 0) {
    // TODO
  }

  dumpByteCodeInfo(&info);

  assert(read16(buf, &info) == 0); // mininez doesn't use tag
  assert(read16(buf, &info) == 0); // mininez doesn't use symbol table

  /*
  ** head is a tmporary variable that indecates the begining
  ** of the instruction sequence
  */
  head = inst = malloc(sizeof(*inst) * info.instSize);
  memset(inst, 0, sizeof(*inst) * info.instSize);

  /* init bytecode loader */
  ByteCodeLoader *loader = malloc(sizeof(ByteCodeLoader));
  loader->input = buf;
  loader->info = &info;
  loader->head = head;

  loadMiniNezInstruction(head, loader, ctx);

  return head;
}
