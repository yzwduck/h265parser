#include "output-context.h"

static void PrintIndent(FILE* fp, int n) {
  static const char* indents[] = {"", "  ", "    ", "      ", "        "};
  while (n) {
    int t = n <= 4 ? n : 4;
    n -= t;
    fputs(indents[t], fp);
  }
}

static int NextIndent(int i) {
  return i <= 0 ? 0 : i + 1;
}

static void DictPrePrint(struct OutputContextDict* ctx, const char* key) {
  if (ctx->first) {
    ctx->first = 0;
  } else {
    fputc(',', ctx->fp);
  }
  if (ctx->indent)
    fputc('\n', ctx->fp);
  PrintIndent(ctx->fp, ctx->indent);
  fprintf(ctx->fp, "\"%s\":", key);
  if (ctx->indent)
    fputc(' ', ctx->fp);
}

static void DictPrintInt(struct OutputContextDict* ctx,
                         const char* key,
                         int64_t val) {
  if (ctx->indent < 0)
    return;
  DictPrePrint(ctx, key);
  fprintf(ctx->fp, "%lld", val);
}

static void DictPrintUint(struct OutputContextDict* ctx,
                          const char* key,
                          uint64_t val) {
  if (ctx->indent < 0)
    return;
  DictPrePrint(ctx, key);
  fprintf(ctx->fp, "%llu", val);
}

static void DictPrintHex(struct OutputContextDict* ctx,
                         const char* key,
                         uint64_t val) {
  if (ctx->indent < 0)
    return;
  if (!ctx->config->print_hex) {
    DictPrintUint(ctx, key, val);
  } else {
    DictPrePrint(ctx, key);
    fprintf(ctx->fp, "0x%llX", val);
  }
}

static void DictPrintEnum(struct OutputContextDict* ctx,
                          const char* key,
                          const char* str,
                          int val) {
  if (ctx->indent < 0)
    return;
  if (!ctx->config->explain_enum) {
    DictPrintUint(ctx, key, val);
  } else {
    DictPrePrint(ctx, key);
    fprintf(ctx->fp, "\"%s (%d)\"", str, val);
  }
}

static void DictPrintStr(struct OutputContextDict* ctx,
                         const char* key,
                         const char* val) {
  if (ctx->indent < 0)
    return;
  DictPrePrint(ctx, key);
  fprintf(ctx->fp, "\"%s\"", val);
}

static void DictPrintDict(struct OutputContextDict* ctx,
                          const char* key,
                          struct OutputContextDict* dict) {
  if (ctx->indent < 0) {
    OutputContextInitDict(dict, ctx->fp, ctx->indent, ctx->config);
  } else {
    DictPrePrint(ctx, key);
    OutputContextInitDict(dict, ctx->fp, NextIndent(ctx->indent), ctx->config);
  }
}

static void DictPrintList(struct OutputContextDict* ctx,
                          const char* key,
                          struct OutputContextList* list) {
  if (ctx->indent < 0) {
    OutputContextInitList(list, ctx->fp, ctx->indent, ctx->config);
  } else {
    DictPrePrint(ctx, key);
    OutputContextInitList(list, ctx->fp, NextIndent(ctx->indent), ctx->config);
  }
}

static void DictEnd(struct OutputContextDict* ctx) {
  if (ctx->indent < 0)
    return;
  if (!ctx->first) {
    if (ctx->indent) {
      fputc('\n', ctx->fp);
      PrintIndent(ctx->fp, ctx->indent - 1);
    }
  }
  fputc('}', ctx->fp);
  ctx->indent = -1;
}

static void ListPrePrint(struct OutputContextList* ctx) {
  if (ctx->first) {
    ctx->first = 0;
  } else {
    fputc(',', ctx->fp);
    if (ctx->indent)
      fputc('\n', ctx->fp);
  }
  PrintIndent(ctx->fp, ctx->indent);
}

static void ListPrintInt(struct OutputContextList* ctx, int64_t val) {
  if (ctx->indent < 0)
    return;
  ListPrePrint(ctx);
  fprintf(ctx->fp, "%lld", val);
}

static void ListPrintUint(struct OutputContextList* ctx, uint64_t val) {
  if (ctx->indent < 0)
    return;
  ListPrePrint(ctx);
  fprintf(ctx->fp, "%llu", val);
}

static void ListPrintHex(struct OutputContextList* ctx, uint64_t val) {
  if (ctx->indent < 0)
    return;
  if (!ctx->config->print_hex) {
    ListPrintUint(ctx, val);
  } else {
    ListPrePrint(ctx);
    fprintf(ctx->fp, "0x%llX", val);
  }
}

static void ListPrintEnum(struct OutputContextList* ctx,
                          const char* str,
                          int val) {
  if (ctx->indent < 0)
    return;
  if (!ctx->config->explain_enum) {
    ListPrintUint(ctx, val);
  } else {
    ListPrePrint(ctx);
    fprintf(ctx->fp, "\"%s (%d)\"", str, val);
  }
}

static void ListPrintStr(struct OutputContextList* ctx, const char* val) {
  if (ctx->indent < 0)
    return;
  ListPrePrint(ctx);
  fprintf(ctx->fp, "\"%s\"", val);
}

static void ListPrintDict(struct OutputContextList* ctx,
                          struct OutputContextDict* dict) {
  if (ctx->indent < 0) {
    OutputContextInitDict(dict, ctx->fp, ctx->indent, ctx->config);
  } else {
    if (ctx->first) {
      ctx->first = 0;
    } else {
      fputc(',', ctx->fp);
      if (ctx->indent)
        fputc(' ', ctx->fp);
    }
    OutputContextInitDict(dict, ctx->fp, NextIndent(ctx->indent), ctx->config);
  }
}

static void ListPrintList(struct OutputContextList* ctx,
                          struct OutputContextList* list) {
  if (ctx->indent < 0) {
    OutputContextInitList(list, ctx->fp, ctx->indent, ctx->config);
  } else {
    if (ctx->first) {
      ctx->first = 0;
    } else {
      fputc(',', ctx->fp);
      if (ctx->indent)
        fputc(' ', ctx->fp);
    }
    OutputContextInitList(list, ctx->fp, NextIndent(ctx->indent), ctx->config);
  }
}

static void ListEnd(struct OutputContextList* ctx) {
  if (ctx->indent < 0)
    return;
  if (!ctx->first) {
    if (ctx->indent) {
      fputc('\n', ctx->fp);
      PrintIndent(ctx->fp, ctx->indent - 1);
    }
  }
  fputc(']', ctx->fp);
  ctx->indent = -1;
}

void OutputContextInitDict(struct OutputContextDict* ctx,
                           FILE* fp,
                           int indent,
                           struct OutputConfig* config) {
  ctx->fp = fp;
  ctx->first = 1;
  ctx->indent = indent;
  ctx->config = config;
  ctx->put_int = DictPrintInt;
  ctx->put_uint = DictPrintUint;
  ctx->put_hex = DictPrintHex;
  ctx->put_enum = DictPrintEnum;
  ctx->put_dict = DictPrintDict;
  ctx->put_list = DictPrintList;
  ctx->end = DictEnd;
  if (ctx->indent >= 0) {
    fputc('{', ctx->fp);
  }
}

void OutputContextInitList(struct OutputContextList* ctx,
                           FILE* fp,
                           int indent,
                           struct OutputConfig* config) {
  ctx->fp = fp;
  ctx->first = 1;
  ctx->indent = indent;
  ctx->config = config;
  ctx->put_int = ListPrintInt;
  ctx->put_uint = ListPrintUint;
  // ctx->put_hex = ListPrintHex;
  // ctx->put_enum = ListPrintEnum;
  ctx->put_dict = ListPrintDict;
  ctx->put_list = ListPrintList;
  ctx->end = ListEnd;
  if (ctx->indent >= 0) {
    fputc('[', ctx->fp);
  }
}
