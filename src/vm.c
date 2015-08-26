#include <stdlib.h>
#include <getopt.h>

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
  ctx->stack_pointer_base =
      (StackEntry)malloc(sizeof(struct StackEntry) * CONTEXT_MAX_STACK_LENGTH);
  ctx->stack_pointer = &ctx->stack_pointer_base[0];
  ctx->stack_size = CONTEXT_MAX_STACK_LENGTH;
  return ctx;
}

static inline StackEntry push_alt(Context ctx, long pos, MiniNezInstruction* jmp, StackEntry fp) {
  ctx->stack_pointer->pos = pos;
  ctx->stack_pointer->jmp = jmp;
  ctx->stack_pointer->failPoint = fp;
  return ctx->stack_pointer++;
}

static inline void push_pos(Context ctx, long pos) {
  (ctx->stack_pointer++)->pos = pos;
}

static inline void push_call(Context ctx, MiniNezInstruction* jmp) {
  (ctx->stack_pointer++)->jmp = jmp;
}

static inline StackEntry pop(Context ctx) {
  return (--ctx->stack_pointer);
}

#define MININEZ_USE_INDIRECT_THREADING 1

long mininez_vm_execute(Context ctx, MiniNezInstruction *inst) {
  register const char *cur = ctx->inputs;
  register MiniNezInstruction *pc;
  register long pos = 0;
  register StackEntry failPoint;

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
#define fail() \
  StackEntry fp = (failPoint);\
  pos = fp->pos;\
  pc = fp->jmp;\
  failPoint = fp->failPoint;\
  ctx->stack_pointer = failPoint;\
  goto *__table[pc->op];
#define JUMP_ADDR(ADDR) JUMP(pc+=ADDR)
#define JUMP(PC) goto *__table[(PC)->op]
#define RET(PC) JUMP(pc = PC)
#else
#error please specify dispatch method
#endif
#endif

#define OP_CASE_(OP) LABEL(OP):

#if MININEZ_DEBUG == 1
#define OP_CASE(OP) OP_CASE_(OP); fprintf(stderr, "%s\n", get_opname(pc->op);
#else
#define OP_CASE(OP) OP_CASE_(OP)
#endif

  failPoint = push_alt(ctx, pos, inst, ctx->stack_pointer);
  push_call(ctx, inst+1);
  pc = inst + 2;
  DISPATCH_START(pc);

  OP_CASE(Iexit) {
    fprintf(stderr, "exit %d\n", pc->arg);
    return pc->arg;
  }
  OP_CASE(Inop) {
    DISPATCH_NEXT();
  }
  OP_CASE(Ifail) {
    fail();
  }
  OP_CASE(Ialt) {
    failPoint = push_alt(ctx, pos, pc+pc->arg, failPoint);
    DISPATCH_NEXT();
  }
  OP_CASE(Isucc) {
    pop(ctx);
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
    StackEntry top = pop(ctx);
    RET(top->jmp);
  }
  OP_CASE(Ipos) {
    push_pos(ctx, pos);
    DISPATCH_NEXT();
  }
  OP_CASE(Iback) {
    pos = pop(ctx)->pos;
    DISPATCH_NEXT();
  }
  OP_CASE(Iskip) {
    pop(ctx);
    DISPATCH_NEXT();
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
    // TODO
  }
  OP_CASE(Iset) {
    // TODO
  }
  OP_CASE(Ilabel) {
    DISPATCH_NEXT();
  }
  return 0;
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
  if(!mininez_vm_execute(ctx, inst)) {
    nez_PrintErrorInfo("parse error!!");
  } else {
    fprintf(stderr, "match!!\n");
  }
  return 0;
}
