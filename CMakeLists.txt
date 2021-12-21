cmake_minimum_required(VERSION 3.10)
project(brainfuck)

set(LUAJIT_PATH "${PROJECT_SOURCE_DIR}/../LuaJIT/")
message("LuaJIT: ${LUAJIT_PATH}")
include_directories(${LUAJIT_PATH})

# brainfuck without jit
add_executable(brainfuck src/brainfuck.c)

if(${CMAKE_SYSTEM_PROCESSOR} STREQUAL "arm64")
# brainfuck arm64 with jit
add_custom_command(OUTPUT brainfuck.arm64.c
  COMMAND ${LUAJIT_PATH}/src/host/minilua ${LUAJIT_PATH}/dynasm/dynasm.lua 
                                          -o brainfuck.arm64.c 
                                          -D ARM64 
                                          ${PROJECT_SOURCE_DIR}/src/brainfuck_arm64.c)
add_executable(brainfuck_arm64 brainfuck.arm64.c)
else()
# brainfuck x86_64 with jit
add_custom_command(OUTPUT brainfuck.x86_64.c
  COMMAND ${LUAJIT_PATH}/src/host/minilua ${LUAJIT_PATH}/dynasm/dynasm.lua 
                                          -o brainfuck.x86_64.c 
                                          -D X64 
                                          ${PROJECT_SOURCE_DIR}/src/brainfuck_x86_64.c)
add_executable(brainfuck_x86_64 brainfuck.x86_64.c)
endif()