#include <stdio.h>
#include <stdint.h>
#include <assert.h>

#ifndef VM_H
#define VM_H

#define MININEZ_DEBUG 0

#define MININEZ_IR_EACH(OP)\
	OP(Iexit)\
	OP(Inop)\
	OP(Ifail)\
	OP(Ialt)\
	OP(Isucc)\
	OP(Ijump)\
	OP(Icall)\
	OP(Iret)\
	OP(Ipos)\
	OP(Iback)\
	OP(Iskip)\
	OP(Ibyte)\
	OP(Iany)\
	OP(Istr)\
	OP(Iset)

enum nezvm_opcode {
#define DEFINE_ENUM(NAME) MININEZ_OP_##NAME,
  MININEZ_IR_EACH(DEFINE_ENUM)
#undef DEFINE_ENUM
  MININEZ_OP_ERROR = -1
};

typedef struct MiniNezInstruction {
	unsigned short op : 5;
	short arg : 11;
} MiniNezInstruction;

struct StackEntry {
  long pos;
  MiniNezInstruction* jmp;
};

struct Context {
  char *inputs;
  size_t input_size;

  size_t stack_size;
  struct StackEntry* stack_pointer;
  struct StackEntry* stack_pointer_base;
};
#define CONTEXT_MAX_STACK_LENGTH 1024

typedef struct StackEntry* StackEntry;
typedef struct Context* Context;

static const char *get_opname(uint8_t opcode) {
  switch (opcode) {
#define OP_DUMPCASE(OP) \
  case MININEZ_OP_##OP:   \
    return "" #OP;
    MININEZ_IR_EACH(OP_DUMPCASE);
  default:
    fprintf(stderr, "%d\n", opcode);
    assert(0 && "UNREACHABLE");
    break;
#undef OP_DUMPCASE
  }
  return "";
}

void nez_PrintErrorInfo(const char *errmsg);
MiniNezInstruction* loadMachineCode(Context ctx, const char* code_file, const char* start_point);

#endif
