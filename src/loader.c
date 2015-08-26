#include <stdlib.h>

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

static uint32_t Loader_Read32(ByteCodeLoader *loader) {
  return read32(loader->input, loader->info);
}

static short Loader_Read16(ByteCodeLoader *loader) {
  return read16(loader->input, loader->info);
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

MiniNezInstruction* loadMachineCode(Context ctx, const char* code_file, const char* start_point) {
  MiniNezInstruction* inst = NULL;
  MiniNezInstruction* head = NULL;
  size_t len;
  char* buf = loadFile(code_file, &len);
  ByteCodeInfo info;
  info.pos = 0;

  fprintf(stderr, "%zu\n", len);
  /* load bytecode header */

  /* load file type */
  info.fileType[0] = read8(buf, &info);
  info.fileType[1] = read8(buf, &info);
  info.fileType[2] = read8(buf, &info);
  info.fileType[3] = 0;

  /* load nez version */
  info.version = read8(buf, &info);
  info.instSize = read16(buf, &info);
  assert(read16(buf, &info) == 0); // mininez doesn't use memo size
  info.jmpTableSize = read16(buf, &info);
  dumpByteCodeInfo(&info);

  info.nonTermPoolSize = read16(buf, &info);
  if(info.nonTermPoolSize > 0) {
    // TODO
  }

  info.setPoolSize = read16(buf, &info);
  if(info.setPoolSize > 0) {
    // TODO
  }

  info.strPoolSize = read16(buf, &info);
  if(info.strPoolSize > 0) {
    // TODO
  }

  assert(read16(buf, &info) == 0); // mininez doesn't use tag
  assert(read16(buf, &info) == 0); // mininez doesn't use symbol table

  return head;
}
