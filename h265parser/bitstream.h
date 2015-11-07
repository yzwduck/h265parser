#ifndef BITSTREAM_H
#define BITSTERAM_H

#include <stdint.h>

struct BitStream {
  uint8_t *buffer_ptr;
  uint8_t *buffer_end;
  uint64_t value;
  uint32_t pos;
  uint32_t shift;
  uint32_t size;
};

void BsInit(struct BitStream *bs, uint8_t *buffer, uint32_t input_size);
void BsSeek(struct BitStream *bs, uint32_t new_pos);
uint32_t BsGet(struct BitStream *bs, uint32_t n);
uint32_t BsPeek(struct BitStream *bs, uint32_t n);
uint32_t BsRemain(struct BitStream *bs);
int BsEof(struct BitStream *bs);
uint32_t BsUe(struct BitStream *bs);
int32_t BsSe(struct BitStream *bs);

#endif
