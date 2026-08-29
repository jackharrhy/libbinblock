#ifndef BINBLOCK_CONTEXT_INTERNAL_H
#define BINBLOCK_CONTEXT_INTERNAL_H

#include <binblock/binblock.h>

bb_status bb_context_allocate(bb_context *context, size_t size, size_t alignment, void **out_pointer);
bb_status bb_context_reallocate(
  bb_context *context,
  void *pointer,
  size_t old_size,
  size_t new_size,
  size_t alignment,
  void **out_pointer
);
void bb_context_deallocate(bb_context *context, void *pointer, size_t size, size_t alignment);
size_t bb_context_allocation_bytes(const bb_context *context);
bb_utf8_policy bb_context_utf8_policy(const bb_context *context);

#endif
