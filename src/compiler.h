#ifndef clang_compiler_h
#define clang_compiler_h

#include "object.h"
#include "vm.h"

ObjFunction *compile(const char *source);
void markCompilerRoots();

#endif // clang_compiler_h
