#include "arena.h"

#include "checked_math.h"
#include "context_internal.h"

#include <stddef.h>

struct bb_arena_block {
  bb_arena_block *next;
  size_t capacity;
  size_t used;
  max_align_t alignment;
  unsigned char data[];
};

static int bb_alignment_is_valid(size_t alignment) {
  return alignment != 0 && (alignment & (alignment - 1)) == 0 && alignment <= _Alignof(max_align_t);
}

static bb_status bb_align_size(size_t value, size_t alignment, size_t *out_aligned) {
  size_t with_padding;
  if (!bb_alignment_is_valid(alignment) || out_aligned == NULL) {
    return BB_STATUS_INVALID_ARGUMENT;
  }
  if (!bb_size_add(value, alignment - 1, &with_padding)) {
    return BB_STATUS_OVERFLOW;
  }
  *out_aligned = with_padding & ~(alignment - 1);
  return BB_STATUS_OK;
}

bb_status bb_arena_init(bb_arena *arena, bb_context *context, size_t default_block_size) {
  if (arena == NULL || context == NULL) {
    return BB_STATUS_INVALID_ARGUMENT;
  }
  arena->context = context;
  arena->blocks = NULL;
  arena->default_block_size = default_block_size == 0 ? 4096 : default_block_size;
  arena->reserved_bytes = 0;
  return BB_STATUS_OK;
}

static bb_status bb_arena_add_block(bb_arena *arena, size_t minimum_capacity) {
  bb_limits limits;
  bb_arena_block *block;
  size_t capacity = arena->default_block_size;
  size_t reserved_bytes;
  size_t allocation_size;
  bb_status status;
  if (capacity < minimum_capacity) {
    capacity = minimum_capacity;
  }
  status = bb_context_get_limits(arena->context, &limits);
  if (status != BB_STATUS_OK) {
    return status;
  }
  if (!bb_size_add(arena->reserved_bytes, capacity, &reserved_bytes)) {
    return BB_STATUS_OVERFLOW;
  }
  if (reserved_bytes > limits.max_arena_bytes) {
    return BB_STATUS_LIMIT_EXCEEDED;
  }
  if (!bb_size_add(offsetof(bb_arena_block, data), capacity, &allocation_size)) {
    return BB_STATUS_OVERFLOW;
  }
  status = bb_context_allocate(
    arena->context,
    allocation_size,
    _Alignof(bb_arena_block),
    (void **)&block
  );
  if (status != BB_STATUS_OK) {
    return status;
  }
  block->next = arena->blocks;
  block->capacity = capacity;
  block->used = 0;
  arena->blocks = block;
  arena->reserved_bytes = reserved_bytes;
  return BB_STATUS_OK;
}

bb_status bb_arena_allocate(bb_arena *arena, size_t size, size_t alignment, void **out_pointer) {
  bb_arena_block *block;
  size_t aligned_used;
  size_t new_used;
  size_t minimum_capacity;
  bb_status status;
  if (arena == NULL || arena->context == NULL || out_pointer == NULL || !bb_alignment_is_valid(alignment)) {
    return BB_STATUS_INVALID_ARGUMENT;
  }
  *out_pointer = NULL;
  if (size == 0) {
    return BB_STATUS_OK;
  }
  block = arena->blocks;
  if (block != NULL) {
    status = bb_align_size(block->used, alignment, &aligned_used);
    if (status != BB_STATUS_OK) {
      return status;
    }
    if (bb_size_add(aligned_used, size, &new_used) && new_used <= block->capacity) {
      block->used = new_used;
      *out_pointer = block->data + aligned_used;
      return BB_STATUS_OK;
    }
  }
  if (!bb_size_add(size, alignment - 1, &minimum_capacity)) {
    return BB_STATUS_OVERFLOW;
  }
  status = bb_arena_add_block(arena, minimum_capacity);
  if (status != BB_STATUS_OK) {
    return status;
  }
  block = arena->blocks;
  status = bb_align_size(block->used, alignment, &aligned_used);
  if (status != BB_STATUS_OK || !bb_size_add(aligned_used, size, &new_used) || new_used > block->capacity) {
    return status == BB_STATUS_OK ? BB_STATUS_INTERNAL_ERROR : status;
  }
  block->used = new_used;
  *out_pointer = block->data + aligned_used;
  return BB_STATUS_OK;
}

void bb_arena_reset(bb_arena *arena) {
  bb_arena_block *block;
  if (arena == NULL || arena->context == NULL) {
    return;
  }
  block = arena->blocks;
  while (block != NULL) {
    bb_arena_block *next = block->next;
    const size_t allocation_size = offsetof(bb_arena_block, data) + block->capacity;
    bb_context_deallocate(arena->context, block, allocation_size, _Alignof(bb_arena_block));
    block = next;
  }
  arena->blocks = NULL;
  arena->reserved_bytes = 0;
}

void bb_arena_destroy(bb_arena *arena) {
  if (arena == NULL) {
    return;
  }
  bb_arena_reset(arena);
  arena->context = NULL;
  arena->default_block_size = 0;
}

size_t bb_arena_reserved_bytes(const bb_arena *arena) {
  return arena == NULL ? 0 : arena->reserved_bytes;
}
