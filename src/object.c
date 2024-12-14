#include "object.h"
#include "chunk.h"
#include "memory.h"
#include "table.h"
#include "value.h"
#include "vm.h"

#include <stdio.h>
#include <string.h>

#define ALLOCATE_OBJ(type, objectType)                                         \
  (type *)allocateObject(sizeof(type), objectType)

static Obj *allocateObject(size_t size, ObjType type) {
  Obj *object = (Obj *)reallocate(NULL, 0, size);
  object->type = type;

  object->next = vm.objects;
  vm.objects = object;
  return object;
}

ObjClosure *newClosure(ObjFunction *function) {
  ObjUpvalue **upvalues = ALLOCATE(ObjUpvalue *, function->upvalueCount);
  for (int i = 0; i < function->upvalueCount; i++) {
    upvalues[i] = NULL;
  }
  ObjClosure *closure = ALLOCATE_OBJ(ObjClosure, OBJ_CLOSURE);
  closure->function = function;
  closure->upvalues = upvalues;
  closure->upvalueCount = function->upvalueCount;
  return closure;
}

ObjFunction *newFunction() {
  ObjFunction *function = ALLOCATE_OBJ(ObjFunction, OBJ_FUNCTION);
  function->arity = 0;
  function->upvalueCount = 0;
  function->name = NULL;
  initChunk(&function->chunk);
  return function;
}

ObjNative *newNative(NativeFn function, int arity) {
  ObjNative *native = ALLOCATE_OBJ(ObjNative, OBJ_NATIVE);
  native->function = function;
  native->arity = arity;
  return native;
}

static ObjString *allocateString(const char *chars, int length, uint32_t hash,
                                 bool ownsChars) {
  // Allocate memory for ObjString and flexible array
  ObjString *string =
      (ObjString *)reallocate(NULL, 0, sizeof(ObjString) + length + 1);

  string->obj.type = OBJ_STRING;
  string->length = length;
  string->hash = hash;
  string->ownsChars = ownsChars;

  // Copy characters into the flexible array (source remains const)
  memcpy(string->chars, chars, length);
  string->chars[length] = '\0';

  // Intern the string
  tableSet(&vm.strings, string, NIL_VAL);
  return string;
}

static uint32_t hashString(const char *key, int length) {
  uint32_t hash = 2166136261u;
  for (int i = 0; i < length; i++) {
    hash ^= (uint8_t)key[i];
    hash *= 16777619;
  }
  return hash;
}

static ObjString *makeString(const char *chars, int length, uint32_t hash,
                             bool ownsChars) {
  // Check if the string already exists in the intern table
  ObjString *interned = tableFindString(&vm.strings, chars, length, hash);
  if (interned != NULL) {
    // If found, return the existing interned string
    return interned;
  }

  // Allocate memory for ObjString and flexible array for characters
  ObjString *string =
      (ObjString *)allocateObject(sizeof(ObjString) + length + 1, OBJ_STRING);

  string->length = length;
  string->hash = hash;
  string->ownsChars = ownsChars;

  // Copy the characters into the flexible array
  memcpy(string->chars, chars, length);
  string->chars[length] = '\0';

  // Add the string to the intern table
  tableSet(&vm.strings, string, NIL_VAL);

  return string;
}

ObjString *takeString(char *chars, int length) {
  uint32_t hash = hashString(chars, length);
  ObjString *string = makeString(chars, length, hash, true);
  FREE_ARRAY(char, chars, length + 1); // Free the original array
  return string;
}

ObjString *copyString(const char *chars, int length) {
  uint32_t hash = hashString(chars, length);
  return makeString(chars, length, hash, true); // Interned and owns its data
}

ObjUpvalue *newUpvalue(Value *slot) {
  ObjUpvalue *upvalue = ALLOCATE_OBJ(ObjUpvalue, OBJ_UPVALUE);
  upvalue->closed = NIL_VAL;
  upvalue->location = slot;
  upvalue->next = NULL;
  return upvalue;
}

static void printFunction(ObjFunction *function) {
  if (function->name == NULL) {
    printf("<script>");
    return;
  }
  printf("<fn %s>", function->name->chars);
}

void printObject(Value value) {
  switch (OBJ_TYPE(value)) {
  case OBJ_CLOSURE:
    printFunction(AS_CLOSURE(value)->function);
    break;
  case OBJ_FUNCTION:
    printFunction(AS_FUNCTION(value));
    break;
  case OBJ_NATIVE:
    printf("<native fn>");
    break;
  case OBJ_STRING:
    printf("%s", AS_CSTRING(value));
    break;
  case OBJ_UPVALUE:
    printf("upvalue");
    break;
  }
}
