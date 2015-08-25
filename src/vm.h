#ifndef VM_H
#define VM_H

#define MININEZ_DEBUG 0

typedef struct MiniNezInstruction {
	unsigned short op : 5;
	short arg : 11;
} MiniNezInstruction;

struct StackEntry {
  long pos;
  unsigned long jmp;
};

struct Context {
  char *inputs;
  size_t input_size;

  size_t stack_size;
  union StackEntry* stack_pointer;
  union StackEntry* stack_pointer_base;
};

typedef struct StackEntry* StackEntry;
typedef struct Context* Context;

#endif
