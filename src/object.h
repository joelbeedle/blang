#ifndef clang_object_h
#define clang_object_h

#include "chunk.h"
#include "common.h"
#include "value.h"

#define OBJ_TYPE(value) (AS_OBJ(value)->type)

#define IS_CLOSURE(value) isObjType(value, OBJ_CLOSURE)
#define IS_FUNCTION(value) isObjType(value, OBJ_FUNCTION)
#define IS_LIST(value) isObjType(value, OBJ_LIST)
#define IS_NATIVE(value) isObjType(value, OBJ_NATIVE)
#define IS_STRING(value) isObjType(value, OBJ_STRING)

#define AS_CLOSURE(value) ((ObjClosure *)AS_OBJ(value))
#define AS_FUNCTION(value) ((ObjFunction *)AS_OBJ(value))
#define AS_LIST(value) ((ObjList *)AS_OBJ(value))
#define AS_NATIVE(value) (((ObjNative *)AS_OBJ(value)))
#define AS_STRING(value) ((ObjString *)AS_OBJ(value))
#define AS_CSTRING(value) (((ObjString *)AS_OBJ(value))->chars)

typedef enum {
  OBJ_CLOSURE,
  OBJ_FUNCTION,
  OBJ_LIST,
  OBJ_NATIVE,
  OBJ_STRING,
  OBJ_UPVALUE
} ObjType;

struct Obj {
  ObjType type;
  bool isMarked;
  struct Obj *next;
};

typedef struct {
  Obj obj;
  int arity;
  int upvalueCount;
  Chunk chunk;
  ObjString *name;
} ObjFunction;

typedef struct {
  Obj obj;
  int count;
  int capacity;
  Value *items;
} ObjList;

typedef struct {
  bool isError;
  Value result;
} NativeResult;

typedef NativeResult (*NativeFn)(int argCount, Value *args);

typedef struct {
  Obj obj;
  int arity;
  NativeFn function;
} ObjNative;

struct ObjString {
  Obj obj;
  int length;
  bool ownsChars;
  uint32_t hash;
  char chars[];
};

typedef struct ObjUpvalue {
  Obj obj;
  Value *location;
  Value closed;
  struct ObjUpvalue *next;
} ObjUpvalue;

typedef struct {
  Obj obj;
  ObjFunction *function;
  ObjUpvalue **upvalues;
  int upvalueCount;
} ObjClosure;

ObjClosure *newClosure(ObjFunction *function);
ObjFunction *newFunction();
ObjList *newList();
void appendToList(ObjList *list, Value value);
void storeToList(ObjList *list, int index, Value value);
Value indexFromList(ObjList *list, int index);
void deleteFromList(ObjList *list, int index);
bool isValidListIndex(ObjList *list, int index);
ObjNative *newNative(NativeFn function, int arity);
ObjString *takeString(char *chars, int length);
ObjString *copyString(const char *chars, int length);
ObjUpvalue *newUpvalue(Value *slot);
void printObject(Value value);

static inline bool isObjType(Value value, ObjType type) {
  return IS_OBJ(value) && AS_OBJ(value)->type == type;
}

#endif // clang_object_h
