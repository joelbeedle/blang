#ifndef clang_compiler_h
#define clang_compiler_h

#include "object.h"
#include "vm.h"

bool compile(const char *source, Chunk *chunk);

#endif // clang_compiler_h
