||#if (defined(__APPLE__) != ARM64)
#error "Wrong DynASM flags used: pass `-D ARM64` to dynasm.lua as appropriate"
#endif
#include <stdio.h>
#include <stdlib.h>
#include "dynasm/dasm_proto.h"
#include "dynasm/dasm_arm64.h"
#include <sys/mman.h>
#if !defined(MAP_ANONYMOUS) && defined(MAP_ANON)
#define MAP_ANONYMOUS MAP_ANON
#endif

#define DASM_CHECKS 1

static void* link_and_encode(dasm_State** d)
{
  int rc;
  size_t sz;
  void* buf;
  dasm_link(d, &sz);
  buf = mmap(0, sz, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  dasm_encode(d, buf);
  rc = mprotect(buf, sz, PROT_READ | PROT_EXEC);
  if (rc != 0) {
    printf("mprotect call failed: rc=%d\n", rc);
    return NULL;
  }
  return buf;
}

#define TAPE_SIZE 30000
#define MAX_NESTING 100

typedef struct bf_state
{
  unsigned char* tape;
  unsigned char (*get_ch)(struct bf_state*);
  void (*put_ch)(struct bf_state*, unsigned char);
} bf_state_t;

#define bad_program(s) exit(fprintf(stderr, "bad program near %.16s: %s\n", program, s))

static void(* bf_compile(const char* program) )(bf_state_t*)
{
  unsigned loops[MAX_NESTING];
  int nloops = 0;
  int n;
  dasm_State* d;
  unsigned npc = 8;
  unsigned nextpc = 0;
  |.arch arm64
  |.section code
  dasm_init(&d, DASM_MAXSECTION);
  |.globals lbl_
  void* labels[lbl__MAX];
  dasm_setupglobal(&d, labels, lbl__MAX);
  |.actionlist bf_actions
  dasm_setup(&d, bf_actions);
  dasm_growpc(&d, npc);

  |.define aPtr, x19
  |.define aState, x20
  |.define aTapeBegin, x21
  |.define aTapeEnd, x22
  |.define aTemp, x23
  |.define aTemp2, x24
  |.define rArg1, x0
  |.define rArg2, x1
  |.macro prepcall1, arg1
    | mov rArg1, arg1
  |.endmacro
  |.macro prepcall2, arg1, arg2
    | mov rArg1, arg1
    | mov rArg2, arg2
  |.endmacro
  |.define postcall, .nop
  |.macro prologue
    | sub sp, sp, #208
    | str aPtr, [sp]
    | str aState, [sp, #8]
    | str aTapeBegin, [sp, #16]
    | str aTapeEnd, [sp, #24]
    | str x23, [sp, #32]
    | str x24, [sp, #40]
    | stp x29, x30, [sp, #48]
    | mov aState, rArg1
  |.endmacro
  |.macro epilogue
    | ldr aPtr, [sp]
    | ldr aState, [sp, #8]
    | ldr aTapeBegin, [sp, #16] 
    | ldr aTapeEnd, [sp, #24]
    | ldr aTemp, [sp, #32]
    | ldr aTemp2, [sp, #40]
    | ldp x29, x30, [sp, #48]
    | add sp, sp, #208
    | ret
  |.endmacro
  
  |.type state, bf_state_t, aState
  
  dasm_State** Dst = &d;
  |.code
  |->bf_main:
  | prologue
  | ldr aPtr, state->tape
  | sub aTapeBegin, aPtr, #1
  | movz aTapeEnd, #TAPE_SIZE-1
  | add aTapeEnd, aTapeEnd, aPtr
  for(;;) {
    switch(*program++) {
    case '<':
      for(n = 1; *program == '<'; ++n, ++program);
      | movz aTemp, #n%TAPE_SIZE
      | sub aPtr, aPtr, aTemp
      | cmp aPtr, aTapeBegin
      | bgt >1
      | movz aTemp, #TAPE_SIZE
      | add aPtr, aPtr, aTemp
      |1:
      break;
    case '>':
      for(n = 1; *program == '>'; ++n, ++program);
      | movz aTemp, #n%TAPE_SIZE
      | add aPtr, aPtr, aTemp
      | cmp aPtr, aTapeEnd
      | blt >1
      | movz aTemp, #TAPE_SIZE
      | sub aPtr, aPtr, aTemp
      |1:
      break;
    case '+':
      for(n = 1; *program == '+'; ++n, ++program);
      // aTemp: x23
      // aTemp2: x24
      | movz w23, #n
      | ldrb w24, [aPtr]
      | add w23, w24, w23
      | strb w23, [aPtr]
      break;
    case '-':
      for(n = 1; *program == '-'; ++n, ++program);
      // aTemp: x23
      // aTemp2: x24
      | movz w23, #n
      | ldrb w24, [aPtr]
      | sub w23, w24, w23
      | strb w23, [aPtr]
      break;
    case ',':
      | prepcall1 aState
      | ldr aTemp, state->get_ch
      | blr aTemp
      | postcall 1
      | strb w0, [aPtr]
      break;
    case '.':
      // aTemp: x23
      | ldrb w23, [aPtr]
      | prepcall2 aState, aTemp
      | ldr aTemp, state->put_ch
      | blr aTemp
      | postcall 2
      break;
    case '[':
      if(nloops == MAX_NESTING)
        bad_program("Nesting too deep");
      if(program[0] == '-' && program[1] == ']') {
        program += 2;
        // aTemp: x23
        | movz aTemp, #0
        | strb w23, [aPtr]
      } else {
        if(nextpc == npc) {
          npc *= 2;
          dasm_growpc(&d, npc);
        }
        // aTemp: x23
        | ldrb w23, [aPtr]
        | cmp w23, #0
        | beq =>nextpc+1
        |=>nextpc:
        loops[nloops++] = nextpc;
        nextpc += 2;
      }
      break;
    case ']':
      if(nloops == 0)
        bad_program("] without matching [");
      --nloops;
      // aTemp: x23
      | ldrb w23, [aPtr]
      | cmp w23, #0
      | bne =>loops[nloops]
      |=>loops[nloops]+1:
      break;
    case 0:
      if(nloops != 0)
        program = "<EOF>", bad_program("[ without matching ]");
      | epilogue
      link_and_encode(&d);
      dasm_free(&d);
      return (void(*)(bf_state_t*))labels[lbl_bf_main];
    }
  }
}

static void bf_putchar(bf_state_t* s, unsigned char c)
{
  putchar((int)c);
}

static unsigned char bf_getchar(bf_state_t* s)
{
  return (unsigned char)getchar();
}

static void bf_run(const char* program)
{
  bf_state_t state;
  unsigned char tape[TAPE_SIZE] = {0};
  state.tape = tape;
  state.get_ch = bf_getchar;
  state.put_ch = bf_putchar;
  bf_compile(program)(&state);
}

int main(int argc, char** argv)
{
  if(argc == 2) {
    long sz;
    char* program;
    FILE* f = fopen(argv[1], "r");
    if(!f) {
      fprintf(stderr, "Cannot open %s\n", argv[1]);
      return 1;
    }
    fseek(f, 0, SEEK_END);
    sz = ftell(f);
    program = (char*)malloc(sz + 1);
    fseek(f, 0, SEEK_SET);
    program[fread(program, 1, sz, f)] = 0;
    fclose(f);
    bf_run(program);
    return 0;
  } else {
    fprintf(stderr, "Usage: %s INFILE.bf\n", argv[0]);
    return 1;
  }
}