#ifndef VM_H
#define VM_H

#define MININEZ_DEBUG 0

typedef struct MiniNezInstruction {
	unsigned short op : 5;
	short arg : 11;
} MiniNezInstruction;

union StackEntry {
  int pos;
  unsigned long jmp;
};

struct Context {
  char *inputs;
  size_t input_size;

  size_t stack_size;
  union StackEntry* stack_pointer;
  union StackEntry* stack_pointer_base;
};

typedef union StackEntry* StackEntry;
typedef union Context* Context;

#endif
