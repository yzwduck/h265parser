#ifndef OUTPUT_CONTEXT_H_
#define OUTPUT_CONTEXT_H_

#include <stdio.h>
#include <stdint.h>

struct OutputContextDict;
struct OutputContextList;

struct OutputConfig {
  uint8_t explain_enum;
  uint8_t print_hex;
};

struct OutputContextDict {
  // public:
  void (*put_int)(struct OutputContextDict* ctx, const char* key, int64_t val);
  void (*put_uint)(struct OutputContextDict* ctx,
                   const char* key,
                   uint64_t val);
  void (*put_hex)(struct OutputContextDict* ctx, const char* key, uint64_t val);
  void (*put_enum)(struct OutputContextDict* ctx,
                   const char* key,
                   const char* str,
                   int val);
  void (*put_str)(struct OutputContextDict* ctx,
                  const char* key,
                  const char* val);
  void (*put_dict)(struct OutputContextDict* ctx,
                   const char* key,
                   struct OutputContextDict* dict);
  void (*put_list)(struct OutputContextDict* ctx,
                   const char* key,
                   struct OutputContextList* list);
  void (*end)(struct OutputContextDict* ctx);
  // private:
  int indent;
  uint8_t first;
  FILE* fp;
  struct OutputConfig* config;
};

struct OutputContextList {
  void (*put_int)(struct OutputContextList* ctx, int64_t val);
  void (*put_uint)(struct OutputContextList* ctx, uint64_t val);
  void (*put_str)(struct OutputContextList* ctx, const char* val);
  void (*put_dict)(struct OutputContextList* ctx,
                   struct OutputContextDict* dict);
  void (*put_list)(struct OutputContextList* ctx,
                   struct OutputContextList* list);
  void (*end)(struct OutputContextList* ctx);
  int indent;
  int first : 1;
  FILE* fp;
  struct OutputConfig* config;
};

void OutputContextInitDict(struct OutputContextDict* ctx,
                           FILE* fp,
                           int indent,
                           struct OutputConfig* config);
void OutputContextInitList(struct OutputContextList* ctx,
                           FILE* fp,
                           int indent,
                           struct OutputConfig* config);

#endif
