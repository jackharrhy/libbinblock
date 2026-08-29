#ifndef BINBLOCK_ARENA_H
#define BINBLOCK_ARENA_H

#include <binblock/binblock.h>

typedef struct bb_arena_block bb_arena_block;

typedef struct bb_arena {
  bb_context *context;
  bb_arena_block *blocks;
  size_t default_block_size;
  size_t reserved_bytes;
} bb_arena;

bb_status bb_arena_init(bb_arena *arena, bb_context *context, size_t default_block_size);
bb_status bb_arena_allocate(bb_arena *arena, size_t size, size_t alignment, void **out_pointer);
void bb_arena_reset(bb_arena *arena);
void bb_arena_destroy(bb_arena *arena);
size_t bb_arena_reserved_bytes(const bb_arena *arena);

#endif
