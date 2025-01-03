#include "vm.h"
#include "chunk.h"
#include "common.h"
#include "compiler.h"
#include "debug.h"
#include "memory.h"
#include "object.h"
#include "value.h"
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define NATIVE_SUCCESS(value)                                                  \
  ((NativeResult){.isError = false, .result = (value)})
#define NATIVE_ERROR(message)                                                  \
  ((NativeResult){.isError = true,                                             \
                  .result =                                                    \
                      OBJ_VAL(copyString((message), (int)strlen(message)))})

VM vm;

static NativeResult clockNative(int argCount, Value *args) {
  return NATIVE_SUCCESS(NUMBER_VAL((double)clock() / CLOCKS_PER_SEC));
}

static void resetStack() {
  vm.stackTop = vm.stack;
  vm.frameCount = 0;
  vm.openUpvalues = NULL;
}

static void runtimeError(const char *format, ...) {
  va_list args;
  va_start(args, format);
  vfprintf(stderr, format, args);
  va_end(args);
  fputs("\n", stderr);

  // Print the call stack
  if (vm.frameCount > 0 && vm.frameCount <= FRAMES_MAX) {
    for (int i = vm.frameCount - 1; i >= 0; i--) {
      CallFrame *frame = &vm.frames[i];
      ObjFunction *function = frame->closure->function;

      if (frame->ip != NULL && frame->ip >= function->chunk.code) {
        size_t instruction = frame->ip - function->chunk.code - 1;
        if (instruction < function->chunk.count) {
          fprintf(stderr, "[line %d] in ",
                  getLine(&function->chunk, instruction));
        } else {
          fprintf(stderr, "[invalid line] in ");
        }
      } else {
        fprintf(stderr, "[unknown line] in ");
      }

      if (function->name == NULL) {
        fprintf(stderr, "script\n");
      } else {
        fprintf(stderr, "%s()\n", function->name->chars);
      }
    }
  } else {
    fprintf(stderr, "Stack corrupted or invalid.\n");
  }

  // Reset the stack and exit cleanly
  resetStack();
}

static NativeResult readFileNative(int argCount, Value *args) {
  if (argCount != 1) {
    return NATIVE_ERROR("readFile() takes exactly 1 argument.");
  }

  if (!IS_STRING(args[0])) {
    return NATIVE_ERROR("Argument to readFile() must be a string.");
  }

  const char *path = AS_CSTRING(args[0]);
  FILE *file = fopen(path, "rb");
  if (file == NULL) {
    return NATIVE_ERROR("Failed to open file.");
  }

  fseek(file, 0L, SEEK_END);
  long fileSize = ftell(file);
  rewind(file);

  char *buffer = (char *)malloc(fileSize + 1);
  if (buffer == NULL) {
    fclose(file);
    return NATIVE_ERROR("Out of memory.");
  }

  size_t bytesRead = fread(buffer, sizeof(char), fileSize, file);
  buffer[bytesRead] = '\0';

  fclose(file);
  Value content = OBJ_VAL(copyString(buffer, bytesRead));
  free(buffer);

  return NATIVE_SUCCESS(content);
}

static NativeResult printlnNative(int argCount, Value *args) {
  for (int i = 0; i < argCount; i++) {
    printValue(args[i]);
    if (i < argCount - 1) {
      printf(" ");
    }
  }
  printf("\n");
  return NATIVE_SUCCESS(NIL_VAL);
}

static NativeResult appendNative(int argCount, Value *args) {
  // Append a value to the end of a list increasing the list's length by 1
  if (argCount != 2 || !IS_LIST(args[0])) {
    return NATIVE_ERROR("append() takes exactly 2 arguments.");
  }
  ObjList *list = AS_LIST(args[0]);
  Value item = args[1];
  appendToList(list, item);
  return NATIVE_SUCCESS(NIL_VAL);
}

static NativeResult deleteNative(int argCount, Value *args) {
  // Delete an item from a list at the given index.
  if (argCount != 2 || !IS_LIST(args[0]) || !IS_NUMBER(args[1])) {
    return NATIVE_ERROR("delete() takes a list and an index as arguments");
  }

  ObjList *list = AS_LIST(args[0]);
  int index = AS_NUMBER(args[1]);

  if (!isValidListIndex(list, index)) {
    return NATIVE_ERROR("Index out of bounds");
  }

  deleteFromList(list, index);
  return NATIVE_SUCCESS(NIL_VAL);
}
static void defineNative(const char *name, NativeFn function, int arity) {
  push(OBJ_VAL(copyString(name, (int)strlen(name))));
  push(OBJ_VAL(newNative(function, arity)));
  tableSet(&vm.globals, AS_STRING(vm.stack[0]), vm.stack[1]);
  pop();
  pop();
}

void initVM() {
  resetStack();
  vm.objects = NULL;

  initTable(&vm.globals);
  initTable(&vm.strings);

  defineNative("clock", clockNative, 0);
  defineNative("readFile", readFileNative, 1);
  defineNative("println", printlnNative, -1);
  defineNative("append", appendNative, 2);
  defineNative("delete", deleteNative, 2);
}

void freeVM() {
  freeTable(&vm.globals);
  freeTable(&vm.strings);
  freeObjects();
}

void push(Value value) {
  *vm.stackTop = value;
  vm.stackTop++;
}

Value pop() {
  vm.stackTop--;
  return *vm.stackTop;
}

static Value peek(int distance) { return vm.stackTop[-1 - distance]; }

static bool call(ObjClosure *closure, int argCount) {

  if (argCount != closure->function->arity) {
    runtimeError("Expected %d arguments but got %d.", closure->function->arity,
                 argCount);
    return false;
  }
  if (vm.frameCount == FRAMES_MAX) {
    runtimeError("Stack overflow.");
    return false;
  }

  CallFrame *frame = &vm.frames[vm.frameCount++];
  frame->closure = closure;
  frame->ip = closure->function->chunk.code;
  frame->slots = vm.stackTop - argCount - 1;
  return true;
}

static inline bool callValue(Value callee, int argCount) {
  if (!IS_OBJ(callee)) {
    runtimeError("Can only call functions and classes.");
    return false;
  }

  Obj *obj = AS_OBJ(callee);

  if (obj->type == OBJ_CLOSURE) {
    return call(AS_CLOSURE(callee), argCount);
  }

  if (obj->type == OBJ_NATIVE) {
    ObjNative *native = (ObjNative *)obj;

    if (native->arity != -1 && native->arity != argCount) {
      runtimeError("Expected %d arguments but got %d.", native->arity,
                   argCount);
      return false;
    }

    NativeResult result = native->function(argCount, vm.stackTop - argCount);

    if (result.isError) {
      runtimeError("Native error: %s", AS_CSTRING(result.result));
      return false;
    }

    vm.stackTop -= argCount + 1;
    *vm.stackTop = result.result;
    vm.stackTop++;
    return true;
  }

  // If not function or native, runtime error
  runtimeError("Can only call functions and classes.");
  return false;
}

static ObjUpvalue *captureUpvalue(Value *local) {
  ObjUpvalue *prevUpvalue = NULL;
  ObjUpvalue *upvalue = vm.openUpvalues;
  while (upvalue != NULL && upvalue->location > local) {
    prevUpvalue = upvalue;
    upvalue = upvalue->next;
  }

  if (upvalue != NULL && upvalue->location == local) {
    return upvalue;
  }
  ObjUpvalue *createdUpvalue = newUpvalue(local);
  createdUpvalue->next = upvalue;

  if (prevUpvalue == NULL) {
    vm.openUpvalues = createdUpvalue;
  } else {
    prevUpvalue->next = createdUpvalue;
  }
  return createdUpvalue;
}

static void closeUpvalues(Value *last) {
  while (vm.openUpvalues != NULL && vm.openUpvalues->location >= last) {
    ObjUpvalue *upvalue = vm.openUpvalues;
    upvalue->closed = *upvalue->location;
    upvalue->location = &upvalue->closed;
    vm.openUpvalues = upvalue->next;
  }
}

static bool isFalsey(Value value) {
  return IS_NIL(value) || (IS_BOOL(value) && !AS_BOOL(value));
}

static void concatenate() {
  ObjString *b = AS_STRING(pop());
  ObjString *a = AS_STRING(pop());

  int length = a->length + b->length;
  char *chars = ALLOCATE(char, length + 1);
  memcpy(chars, a->chars, a->length);
  memcpy(chars + a->length, b->chars, b->length);
  chars[length] = '\0';

  ObjString *result = takeString(chars, length);
  push(OBJ_VAL(result));
}

static InterpretResult run() {
  CallFrame *frame = &vm.frames[vm.frameCount - 1];
  register uint8_t *ip = frame->ip;

#define READ_BYTE() (*ip++)
#define READ_SHORT() (ip += 2, (uint16_t)((ip[-2] << 8) | ip[-1]))
#define READ_CONSTANT()                                                        \
  (frame->closure->function->chunk.constants.values[READ_BYTE()])
#define READ_STRING() AS_STRING(READ_CONSTANT())
#define BINARY_OP(valueType, op)                                               \
  do {                                                                         \
    if (!IS_NUMBER(peek(0)) || !IS_NUMBER(peek(1))) {                          \
      frame->ip = ip;                                                          \
      runtimeError("Operands must be numbers.");                               \
      return INTERPRET_RUNTIME_ERROR;                                          \
    }                                                                          \
    double b = AS_NUMBER(pop());                                               \
    double a = AS_NUMBER(pop());                                               \
    push(valueType(a op b));                                                   \
  } while (false)
#define BINARY_OP_IN_PLACE(op)                                                 \
  do {                                                                         \
    if (!IS_NUMBER(peek(0)) || !IS_NUMBER(peek(1))) {                          \
      frame->ip = ip;                                                          \
      runtimeError("Operands must be numbers.");                               \
      return INTERPRET_RUNTIME_ERROR;                                          \
    }                                                                          \
    vm.stackTop[-2] =                                                          \
        NUMBER_VAL(AS_NUMBER(vm.stackTop[-2]) op AS_NUMBER(vm.stackTop[-1]));  \
    vm.stackTop--; /* Adjust stack pointer */                                  \
  } while (false)
  for (;;) {

#ifdef DEBUG_TRACE_EXECUTION
    printf("          ");
    for (Value *slot = vm.stack; slot < vm.stackTop; slot++) {
      printf("[ ");
      printValue(*slot);
      printf(" ]");
    }
    printf("\n");

    disassembleInstruction(
        &frame->closure->function->chunk,
        (int)(frame->ip - frame->closure->function->chunk.code));
#endif
    uint8_t instruction;
    switch (instruction = READ_BYTE()) {
    case OP_CONSTANT: {
      Value constant = READ_CONSTANT();
      push(constant);
      break;
    }
    case OP_NIL:
      push(NIL_VAL);
      break;
    case OP_TRUE:
      push(BOOL_VAL(true));
      break;
    case OP_FALSE:
      push(BOOL_VAL(false));
      break;
    case OP_POP:
      pop();
      break;
    case OP_DUP:
      push(peek(0));
      break;
    case OP_GET_LOCAL: {
      uint8_t slot = READ_BYTE();
      push(frame->slots[slot]);
      break;
    }
    case OP_SET_LOCAL: {
      uint8_t slot = READ_BYTE();
      frame->slots[slot] = peek(0);
      break;
    }
    case OP_GET_GLOBAL: {
      ObjString *name = READ_STRING();
      Value value;
      if (!tableGet(&vm.globals, name, &value)) {
        frame->ip = ip;
        runtimeError("Undefined variable '%s'", name->chars);
        return INTERPRET_RUNTIME_ERROR;
      }
      push(value);
      break;
    }
    case OP_DEFINE_GLOBAL: {
      ObjString *name = READ_STRING();
      tableSet(&vm.globals, name, peek(0));
      pop();
      break;
    }
    case OP_SET_GLOBAL: {
      ObjString *name = READ_STRING();
      if (tableSet(&vm.globals, name, peek(0))) {
        tableDelete(&vm.globals, name);
        frame->ip = ip;
        runtimeError("Undefined variable '%s'.", name->chars);
        return INTERPRET_RUNTIME_ERROR;
      }
      break;
    }
    case OP_GET_UPVALUE: {
      uint8_t slot = READ_BYTE();
      push(*frame->closure->upvalues[slot]->location);
      break;
    }
    case OP_SET_UPVALUE: {
      uint8_t slot = READ_BYTE();
      *frame->closure->upvalues[slot]->location = peek(0);
      break;
    }
    case OP_EQUAL: {
      vm.stackTop[-2] = BOOL_VAL(valuesEqual(vm.stackTop[-2], vm.stackTop[-1]));
      vm.stackTop--;
      break;
    }
    case OP_GREATER:
      BINARY_OP(BOOL_VAL, >);
      break;
    case OP_LESS:
      BINARY_OP(BOOL_VAL, <);
      break;
    case OP_ADD: {
      if (IS_STRING(peek(0)) && IS_STRING(peek(1))) {
        concatenate();
      } else if (IS_NUMBER(peek(0)) && IS_NUMBER(peek(1))) {
        vm.stackTop[-2] =
            NUMBER_VAL(AS_NUMBER(vm.stackTop[-2]) + AS_NUMBER(vm.stackTop[-1]));
        vm.stackTop--;
      } else {
        frame->ip = ip;
        runtimeError("Operands must be two numbers or two strings");
        return INTERPRET_RUNTIME_ERROR;
      }
      break;
    }
    case OP_SUBTRACT:
      BINARY_OP_IN_PLACE(-);
      break;
    case OP_MULTIPLY:
      BINARY_OP_IN_PLACE(*);
      break;
    case OP_DIVIDE:
      BINARY_OP_IN_PLACE(/);
      break;
    case OP_NOT:
      vm.stackTop[-1] = BOOL_VAL(isFalsey(vm.stackTop[-1]));
      break;
    case OP_NEGATE:
      if (!IS_NUMBER(peek(0))) {
        frame->ip = ip;
        runtimeError("Operand must be a number");
        return INTERPRET_RUNTIME_ERROR;
      }
      vm.stackTop[-1] = NUMBER_VAL(-AS_NUMBER(vm.stackTop[-1]));
      // push(NUMBER_VAL(-AS_NUMBER(pop())));
      break;
    case OP_PRINT: {
      printValue(pop());
      printf("\n");
      break;
    }
    case OP_JUMP: {
      uint16_t offset = READ_SHORT();
      ip += offset;
      break;
    }
    case OP_JUMP_IF_FALSE: {
      uint16_t offset = READ_SHORT();
      if (isFalsey(peek(0)))
        ip += offset;
      break;
    }
    case OP_LOOP: {
      uint16_t offset = READ_SHORT();
      ip -= offset;
      break;
    }
    case OP_CALL: {
      int argCount = READ_BYTE();
      frame->ip = ip;
      if (!callValue(peek(argCount), argCount)) {
        return INTERPRET_RUNTIME_ERROR;
      }
      frame = &vm.frames[vm.frameCount - 1];
      ip = frame->ip;
      break;
    }
    case OP_CLOSURE: {
      ObjFunction *function = AS_FUNCTION(READ_CONSTANT());
      ObjClosure *closure = newClosure(function);
      push(OBJ_VAL(closure));
      for (int i = 0; i < closure->upvalueCount; i++) {
        uint8_t isLocal = READ_BYTE();
        uint8_t index = READ_BYTE();
        if (isLocal) {
          closure->upvalues[i] = captureUpvalue(frame->slots + index);
        } else {
          closure->upvalues[i] = frame->closure->upvalues[index];
        }
      }
      break;
    }
    case OP_BUILD_LIST: {
      // Stack before: [item1, item2, ..., itemN] and after: [list]
      ObjList *list = newList();
      uint8_t itemCount = READ_BYTE();

      // Add items to list
      push(OBJ_VAL(list)); // So list isn't sweeped by GC in appendToList
      for (int i = itemCount; i > 0; i--) {
        appendToList(list, peek(i));
      }
      pop();

      // Pop items from stack
      while (itemCount-- > 0) {
        pop();
      }

      push(OBJ_VAL(list));
      break;
    }
    case OP_INDEX_SUBSCR: {
      // Stack before: [list, index] and after: [index(list, index)]
      Value oldIndex = pop();
      Value oldList = pop();
      Value result;

      if (!IS_LIST(oldList)) {
        runtimeError("Invalid type to index into.");
        return INTERPRET_RUNTIME_ERROR;
      }
      ObjList *list = AS_LIST(oldList);

      if (!IS_NUMBER(oldIndex)) {
        runtimeError("List index is not a number.");
        return INTERPRET_RUNTIME_ERROR;
      }
      int index = AS_NUMBER(oldIndex);

      if (!isValidListIndex(list, index)) {
        runtimeError("List index out of range.");
        return INTERPRET_RUNTIME_ERROR;
      }

      result = indexFromList(list, index);
      push(result);
      break;
    }
    case OP_STORE_SUBSCR: {
      // Stack before: [list, index, item] and after: [item]
      Value item = pop();
      Value oldIndex = pop();
      Value oldList = pop();

      if (!IS_LIST(oldList)) {
        runtimeError("Cannot store value in a non-list.");
        return INTERPRET_RUNTIME_ERROR;
      }
      ObjList *list = AS_LIST(oldList);

      if (!IS_NUMBER(oldIndex)) {
        runtimeError("List index is not a number.");
        return INTERPRET_RUNTIME_ERROR;
      }
      int index = AS_NUMBER(oldIndex);

      if (!isValidListIndex(list, index)) {
        runtimeError("Invalid list index.");
        return INTERPRET_RUNTIME_ERROR;
      }

      storeToList(list, index, item);
      push(item);
      break;
    }
    case OP_CLOSE_UPVALUE:
      closeUpvalues(vm.stackTop - 1);
      pop();
      break;
    case OP_RETURN: {
      Value result = pop();
      closeUpvalues(frame->slots);
      vm.frameCount--;
      if (vm.frameCount == 0) {
        pop();
        return INTERPRET_OK;
      }

      vm.stackTop = frame->slots;
      push(result);
      frame = &vm.frames[vm.frameCount - 1];
      ip = frame->ip;
      break;
    }
    }
  }
#undef READ_BYTE
#undef READ_CONSTANT
#undef READ_SHORT
#undef READ_STRING
#undef BINARY_OP_IN_PLACE
#undef BINARY_OP
}

InterpretResult interpret(const char *source) {
  ObjFunction *function = compile(source);
  if (function == NULL)
    return INTERPRET_COMPILE_ERROR;

  push(OBJ_VAL(function));
  ObjClosure *closure = newClosure(function);
  pop();
  push(OBJ_VAL(closure));
  call(closure, 0);

  return run();
}
