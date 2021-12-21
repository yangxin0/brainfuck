### Brainfuck

This project implemented a simple brainfuck interpreter with JIT which is should be used for demo purposes. There are two brainfuck implementations with or without JIT and they are named brainfuck.c and brainfuck_arm64.c respectively. This project does not include the LuaJIT project and you should set up LuaJIT on your own.

### Build with JIT

By default brainfuck and LuaJIT should be put in the same directory but you can customize the LuaJIT path with your need by `-DLUA_JIT=` CMake parameter.

```shell
# change to brainfuck source directory firstly
mkdir build
cd build && cmake ..
make 

# run brainfuck without jit
./brainfuck ../test/mandelbrot.bf

# run brainfuck with jit
./brainfuck_arm64 ../test/mandelbrot.bf
```

### Turtorial

The official LuaJIT document is incomplete but there is an unofficial document that explains LuaJIT in detail.

[Unofficial Turtorial](https://corsix.github.io/dynasm-doc/tutorial.html)



