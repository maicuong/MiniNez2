#include <stdlib.h>
#include <getopt.h>
#include <sys/time.h> // gettimeofday

#include "vm.h"

char *loadFile(const char *filename, size_t *length);

void nez_PrintErrorInfo(const char *errmsg) {
  fprintf(stderr, "%s\n", errmsg);
  exit(EXIT_FAILURE);
}

Context mininez_CreateContext(const char *filename) {
  Context ctx = (Context)malloc(sizeof(struct Context));
  ctx->input_size = 0;
  ctx->inputs = loadFile(filename, &ctx->input_size);
#if USE_STACK_ENTRY == 1
  ctx->stack_pointer_base =
      (StackEntry)malloc(sizeof(struct StackEntry) * CONTEXT_MAX_STACK_LENGTH);
  ctx->stack_pointer = &ctx->stack_pointer_base[0];
#else
  ctx->stack_pointer_base =
      (long* )malloc(sizeof(long) * CONTEXT_MAX_STACK_LENGTH);
  ctx->stack_pointer = &ctx->stack_pointer_base[0];
#endif
  ctx->stack_size = CONTEXT_MAX_STACK_LENGTH;
  return ctx;
}

#if STACK_USAGE == 1
int max_stack_size = 0;
#endif

#if USE_STACK_ENTRY == 1
static inline StackEntry push_alt(Context ctx, long pos, MiniNezInstruction* jmp, StackEntry fp) {
  ctx->stack_pointer->pos = pos;
  ctx->stack_pointer->jmp = jmp;
  ctx->stack_pointer->failPoint = fp;
#if STACK_USAGE == 1
  int used = ctx->stack_pointer - ctx->stack_pointer_base;
  if(used > max_stack_size) {
    max_stack_size = used;
  }
#endif
  return ctx->stack_pointer++;
}
#else
static inline long* push_alt(Context ctx, long pos, MiniNezInstruction* jmp, long* fp) {
  long* ret = ctx->stack_pointer;
  ctx->stack_pointer[0] = pos;
  ctx->stack_pointer[1] = (long)jmp;
  ctx->stack_pointer[2] = (long)fp;
  ctx->stack_pointer += 3;
#if STACK_USAGE == 1
  int used = ctx->stack_pointer - ctx->stack_pointer_base;
  if(used > max_stack_size) {
    max_stack_size = used;
  }
#endif
  return ret;
}
#endif

static inline void push_pos(Context ctx, long pos) {
#if USE_STACK_ENTRY == 1
#if STACK_USAGE == 1
  int used = ctx->stack_pointer - ctx->stack_pointer_base;
  if(used > max_stack_size) {
    max_stack_size = used;
  }
#endif
  (ctx->stack_pointer++)->pos = pos;
#else
  ctx->stack_pointer[0] = pos;
  ctx->stack_pointer++;
#if STACK_USAGE == 1
  int used = ctx->stack_pointer - ctx->stack_pointer_base;
  if(used > max_stack_size) {
    max_stack_size = used;
  }
#endif
#endif
}

static inline void push_call(Context ctx, MiniNezInstruction* jmp) {
#if USE_STACK_ENTRY == 1
#if STACK_USAGE == 1
  int used = ctx->stack_pointer - ctx->stack_pointer_base;
  if(used > max_stack_size) {
    max_stack_size = used;
  }
#endif
  (ctx->stack_pointer++)->jmp = jmp;
#else
  ctx->stack_pointer[0] = (long)jmp;
  ctx->stack_pointer++;
#if STACK_USAGE == 1
  int used = ctx->stack_pointer - ctx->stack_pointer_base;
  if(used > max_stack_size) {
    max_stack_size = used;
  }
#endif
#endif
}

#if USE_STACK_ENTRY == 1
static inline StackEntry peek(Context ctx) {
  return ctx->stack_pointer;
}

static inline MiniNezInstruction* pop_jmp(Context ctx) {
  return (--ctx->stack_pointer)->jmp;
}

static inline long pop_pos(Context ctx) {
  return (--ctx->stack_pointer)->pos;
}
#else
static inline long* peek(Context ctx) {
  return ctx->stack_pointer;
}

static inline MiniNezInstruction* pop_jmp(Context ctx) {
  --ctx->stack_pointer;
  return (MiniNezInstruction*)ctx->stack_pointer[0];
}

static inline long pop_pos(Context ctx) {
  return *(--ctx->stack_pointer);
}
#endif

#define MININEZ_USE_INDIRECT_THREADING 1

long mininez_vm_execute(Context ctx, MiniNezInstruction *inst) {
  register const char *cur = ctx->inputs;
  register MiniNezInstruction *pc;
  register long pos = 0;
#if USE_STACK_ENTRY == 1
  register StackEntry failPoint = ctx->stack_pointer;
#else
  register long* failPoint = ctx->stack_pointer;
#endif

#ifdef MININEZ_USE_SWITCH_CASE_DISPATCH
#define DISPATCH_NEXT()         goto L_vm_head
#define DISPATCH_START(PC) L_vm_head:;switch (*PC++) {
#define DISPATCH_END()     default: ABORT(); }
#define LABEL(OP)          case OP
#else
#define LABEL(OP)          MININEZ_OP_##OP
    static const void *__table[] = {
#define DEFINE_TABLE(OP) &&LABEL(OP),
        MININEZ_IR_EACH(DEFINE_TABLE)
#undef DEFINE_TABLE
    };
#define DISPATCH_START(PC) DISPATCH_NEXT()

#if defined(MININEZ_USE_INDIRECT_THREADING)
#define DISPATCH_NEXT() goto *__table[(++pc)->op]
#define fail() goto L_fail
#if USE_STACK_ENTRY == 1
#define FAIL_IMPL() do {\
  StackEntry fp = (failPoint);\
  pos = fp->pos;\
  pc = fp->jmp;\
  failPoint = fp->failPoint;\
  ctx->stack_pointer = fp;\
  goto *__table[pc->op];\
} while(0)
#else
#define FAIL_IMPL() do {\
  long* fp = (failPoint);\
  pos = fp[0];\
  pc = (MiniNezInstruction *)fp[1];\
  failPoint = (long*)fp[2];\
  ctx->stack_pointer = fp;\
  goto *__table[pc->op];\
} while(0)
#endif
#define JUMP_ADDR(ADDR) JUMP(pc = inst + ADDR)
#define JUMP(PC) goto *__table[(PC)->op]
#define RET(PC) JUMP(pc = PC)
#else
#error please specify dispatch method
#endif
#endif

#define OP_CASE_(OP) LABEL(OP):

#if MININEZ_DEBUG == 1
#define OP_CASE(OP) OP_CASE_(OP); fprintf(stderr, "[%ld] %s (pos:%ld)\n", pc - inst, get_opname(pc->op), pos);
#else
#define OP_CASE(OP) OP_CASE_(OP)
#endif

  failPoint = push_alt(ctx, pos, inst, ctx->stack_pointer);
  push_call(ctx, inst+1);
  pc = inst + 1;
  DISPATCH_START(pc);

  OP_CASE(Iexit) {
    ctx->pos = pos;
#if MININEZ_DEBUG == 1
    fprintf(stderr, "exit %d\n", pc->arg);
#endif
#if STACK_USAGE == 1
    fprintf(stderr, "stack_usage: %lu[byte]\n", max_stack_size * sizeof(*ctx->stack_pointer));
#endif
    return pc->arg;
  }
  OP_CASE(Inop) {
    DISPATCH_NEXT();
  }
  OP_CASE(Ifail) {
  L_fail:
    FAIL_IMPL();
  }
  OP_CASE(Ialt) {
    failPoint = push_alt(ctx, pos, inst+pc->arg, failPoint);
    DISPATCH_NEXT();
  }
  OP_CASE(Isucc) {
    ctx->stack_pointer = failPoint;
#if USE_STACK_ENTRY == 1
    failPoint = failPoint->failPoint;
#else
    failPoint = (long*)failPoint[2];
#endif
    DISPATCH_NEXT();
  }
  OP_CASE(Ijump) {
    JUMP_ADDR(pc->arg);
  }
  OP_CASE(Icall) {
    push_call(ctx, pc+1);
    JUMP_ADDR(pc->arg);
  }
  OP_CASE(Iret) {
    MiniNezInstruction* tmp = pop_jmp(ctx);
    RET(tmp);
  }
  OP_CASE(Ipos) {
    push_pos(ctx, pos);
    DISPATCH_NEXT();
  }
  OP_CASE(Iback) {
    pos = pop_pos(ctx);
    DISPATCH_NEXT();
  }
  OP_CASE(Iskip) {
#if USE_STACK_ENTRY == 1
    StackEntry top = peek(ctx);
    if(pos == top->pos) {
      fail();
    }
    failPoint->pos = pos;
#else
    long* top = peek(ctx);
    if(pos == *top) {
      fail();
    }
    failPoint[0] = pos;
#endif
    JUMP_ADDR(pc->arg);
  }
  OP_CASE(Ibyte) {
    if(cur[pos] != pc->arg) {
      fail();
    }
    ++pos;
    DISPATCH_NEXT();
  }
  OP_CASE(Iany) {
    if(cur[pos] == 0) {
      fail();
    }
    ++pos;
    DISPATCH_NEXT();
  }
  OP_CASE(Istr) {
    const char* str = ctx->strs[pc->arg];
    unsigned len = pstring_length(str);
    if (pstring_starts_with(cur+pos, str, len) == 0) {
      fail();
    }
    pos += len;
    DISPATCH_NEXT();
  }
  OP_CASE(Iset) {
    bitset_t set = ctx->sets[pc->arg];
    if (!bitset_get(&set, cur[pos])) {
      fail();
    }
    ++pos;
    DISPATCH_NEXT();
  }
  OP_CASE(Inbyte) {
    if(cur[pos] != pc->arg) {
      DISPATCH_NEXT();
    }
    fail();
  }
	OP_CASE(Instr) {
    const char* str = ctx->strs[pc->arg];
    unsigned len = pstring_length(str);
    if (pstring_starts_with(cur+pos, str, len) == 0) {
      DISPATCH_NEXT();
    }
    fail();
  }
  OP_CASE(Iostr) {
    const char* str = ctx->strs[pc->arg];
    unsigned len = pstring_length(str);
    if (pstring_starts_with(cur+pos, str, len) == 0) {
      DISPATCH_NEXT();
    }
    pos += len;
    DISPATCH_NEXT();
  }
  OP_CASE(Ioset) {
    bitset_t set = ctx->sets[pc->arg];
    if (!bitset_get(&set, cur[pos])) {
      DISPATCH_NEXT();
    }
    ++pos;
    DISPATCH_NEXT();
  }
  OP_CASE(Irset) {
    while(1) {
      bitset_t set = ctx->sets[pc->arg];
      if (!bitset_get(&set, cur[pos])) {
        break;
      }
      ++pos;
    }
    DISPATCH_NEXT();
  }
  OP_CASE(Ilabel) {
#if MININEZ_DEBUG == 1
    fprintf(stderr, "%s\n", ctx->nterms[pc->arg]);
#endif
    DISPATCH_NEXT();
  }
  return 0;
}

static uint64_t timer() {
  struct timeval tv;
  gettimeofday(&tv, NULL);
  return tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

static void nez_ShowUsage(const char *file) {
  // fprintf(stderr, "Usage: %s -f nez_bytecode target_file\n", file);
  fprintf(stderr, "\nnezvm <command> optional files\n");
  fprintf(stderr, "  -p <filename> Specify an PEGs grammar bytecode file\n");
  fprintf(stderr, "  -i <filename> Specify an input file\n");
  fprintf(stderr, "  -o <filename> Specify an output file\n");
  fprintf(stderr, "  -t <type>     Specify an output type\n");
  fprintf(stderr, "  -h            Display this help and exit\n\n");
  exit(EXIT_FAILURE);
}

int main(int argc, char *const argv[]) {
  Context ctx = NULL;
  MiniNezInstruction *inst = NULL;
  const char *syntax_file = NULL;
  const char *input_file = NULL;
  const char *output_type = NULL;
  const char *orig_argv0 = argv[0];
  int opt;
  while ((opt = getopt(argc, argv, "p:i:t:o:c:h:")) != -1) {
    switch (opt) {
    case 'p':
      syntax_file = optarg;
      break;
    case 'i':
      input_file = optarg;
      break;
    case 't':
      output_type = optarg;
      break;
    case 'h':
      nez_ShowUsage(orig_argv0);
    default: /* '?' */
      nez_ShowUsage(orig_argv0);
    }
  }
  if (syntax_file == NULL) {
    nez_PrintErrorInfo("not input syntaxfile");
  }
  ctx = mininez_CreateContext(input_file);
  inst = loadMachineCode(ctx, syntax_file, "File");
#if MININEZ_LOAD_DEBUG == 0
  uint64_t start, end;
  start = timer();
  if(!mininez_vm_execute(ctx, inst)) {
    nez_PrintErrorInfo("parse error!!");
  } else if(ctx->pos != (long)ctx->input_size) {
    fprintf(stderr, "unconsumed!! pos=%ld size=%zu", ctx->pos, ctx->input_size);
  } else {
    fprintf(stderr, "match!!\n");
  }
  end = timer();
  fprintf(stderr, "ErapsedTime: %llu msec\n", (unsigned long long)end - start);
#endif
  return 0;
}
