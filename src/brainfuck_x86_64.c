||#if ((defined(_M_X64) || defined(__amd64__)) != X64) || (defined(_WIN32) != WIN)
#error "Wrong DynASM flags used: pass `-D X64` and/or `-D WIN` to dynasm.lua as appropriate"
#endif
#include <stdio.h>
#include <stdlib.h>
#include "dynasm/dasm_proto.h"
#include "dynasm/dasm_x86.h"
#if _WIN32
#include <Windows.h>
#else
#include <sys/mman.h>
#if !defined(MAP_ANONYMOUS) && defined(MAP_ANON)
#define MAP_ANONYMOUS MAP_ANON
#endif
#endif

static void* link_and_encode(dasm_State** d)
{
  size_t sz;
  void* buf;
  dasm_link(d, &sz);
#ifdef _WIN32
  buf = VirtualAlloc(0, sz, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
#else
  buf = mmap(0, sz, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
#endif
  dasm_encode(d, buf);
#ifdef _WIN32
  {DWORD dwOld; VirtualProtect(buf, sz, PAGE_EXECUTE_READ, &dwOld); }
#else
  mprotect(buf, sz, PROT_READ | PROT_EXEC);
#endif
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
  |.if X64
  |.arch x64
  |.else
  |.arch x86
  |.endif
  |.section code
  dasm_init(&d, DASM_MAXSECTION);
  |.globals lbl_
  void* labels[lbl__MAX];
  dasm_setupglobal(&d, labels, lbl__MAX);
  |.actionlist bf_actions
  dasm_setup(&d, bf_actions);
  dasm_growpc(&d, npc);
  |.if X64
    |.define aPtr, rbx
    |.define aState, r12
    |.if WIN
      |.define aTapeBegin, rsi
      |.define aTapeEnd, rdi
      |.define rArg1, rcx
      |.define rArg2, rdx
    |.else
      |.define aTapeBegin, r13
      |.define aTapeEnd, r14
      |.define rArg1, rdi
      |.define rArg2, rsi
    |.endif
    |.macro prepcall1, arg1
      | mov rArg1, arg1
    |.endmacro
    |.macro prepcall2, arg1, arg2
      | mov rArg1, arg1
      | mov rArg2, arg2
    |.endmacro
    |.define postcall, .nop
    |.macro prologue
      | push aPtr
      | push aState
      | push aTapeBegin
      | push aTapeEnd
      | push rax
      | mov aState, rArg1
    |.endmacro
    |.macro epilogue
      | pop rax
      | pop aTapeEnd
      | pop aTapeBegin
      | pop aState
      | pop aPtr
      | ret
    |.endmacro
  |.else
    |.define aPtr, ebx
    |.define aState, ebp
    |.define aTapeBegin, esi
    |.define aTapeEnd, edi
    |.macro prepcall1, arg1
      | push arg1
    |.endmacro
    |.macro prepcall2, arg1, arg2
      | push arg2
      | push arg1
    |.endmacro
    |.macro postcall, n
      | add esp, 4*n
    |.endmacro
    |.macro prologue
      | push aPtr
      | push aState
      | push aTapeBegin
      | push aTapeEnd
      | mov aState, [esp+20]
    |.endmacro
    |.macro epilogue
      | pop aTapeEnd
      | pop aTapeBegin
      | pop aState
      | pop aPtr
      | ret 4
    |.endmacro
  |.endif

  |.type state, bf_state_t, aState
  
  dasm_State** Dst = &d;
  |.code
  |->bf_main:
  | prologue
  | mov aPtr, state->tape
  | lea aTapeBegin, [aPtr-1]
  | lea aTapeEnd, [aPtr+TAPE_SIZE-1]
  for(;;) {
    switch(*program++) {
    case '<':
      for(n = 1; *program == '<'; ++n, ++program);
      | sub aPtr, n%TAPE_SIZE
      | cmp aPtr, aTapeBegin
      | ja >1
      | add aPtr, TAPE_SIZE
      |1:
      break;
    case '>':
      for(n = 1; *program == '>'; ++n, ++program);
      | add aPtr, n%TAPE_SIZE
      | cmp aPtr, aTapeEnd
      | jbe >1
      | sub aPtr, TAPE_SIZE
      |1:
      break;
    case '+':
      for(n = 1; *program == '+'; ++n, ++program);
      | add byte [aPtr], n
      break;
    case '-':
      for(n = 1; *program == '-'; ++n, ++program);
      | sub byte [aPtr], n
      break;
    case ',':
      | prepcall1 aState
      | call aword state->get_ch
      | postcall 1
      | mov byte [aPtr], al
      break;
    case '.':
      | movzx r0, byte [aPtr]
      | prepcall2 aState, r0
      | call aword state->put_ch
      | postcall 2
      break;
    case '[':
      if(nloops == MAX_NESTING)
        bad_program("Nesting too deep");
      if(program[0] == '-' && program[1] == ']') {
        program += 2;
        | xor eax, eax
        | mov byte [aPtr], al
      } else {
        if(nextpc == npc) {
          npc *= 2;
          dasm_growpc(&d, npc);
        }
        | cmp byte [aPtr], 0
        | jz =>nextpc+1
        |=>nextpc:
        loops[nloops++] = nextpc;
        nextpc += 2;
      }
      break;
    case ']':
      if(nloops == 0)
        bad_program("] without matching [");
      --nloops;
      | cmp byte [aPtr], 0
      | jnz =>loops[nloops]
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