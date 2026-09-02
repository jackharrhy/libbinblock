#include <binblock/binblock.h>

#include "checked_math.h"
#include "context_internal.h"

#include <stdlib.h>
#include <string.h>

typedef struct bb_source_record {
  uint8_t *storage;
  size_t storage_size;
  bb_string_view name;
  bb_bytes contents;
} bb_source_record;

struct bb_context {
  bb_allocator allocator;
  bb_limits limits;
  bb_utf8_policy utf8_policy;
  void *log_user;
  bb_log_fn log;
  size_t allocation_bytes;
  bb_source_record *sources;
  size_t source_count;
  size_t source_capacity;
};

static void *bb_default_alloc(void *user, size_t size, size_t alignment) {
  (void)user;
  (void)alignment;
  return malloc(size);
}

static void *bb_default_realloc(void *user, void *pointer, size_t old_size, size_t new_size, size_t alignment) {
  (void)user;
  (void)old_size;
  (void)alignment;
  return realloc(pointer, new_size);
}

static void bb_default_free(void *user, void *pointer, size_t size, size_t alignment) {
  (void)user;
  (void)size;
  (void)alignment;
  free(pointer);
}

static bb_allocator bb_default_allocator(void) {
  bb_allocator allocator;
  allocator.user = NULL;
  allocator.alloc = bb_default_alloc;
  allocator.realloc = bb_default_realloc;
  allocator.free = bb_default_free;
  return allocator;
}

static int bb_allocator_is_empty(const bb_allocator *allocator) {
  return allocator->user == NULL && allocator->alloc == NULL && allocator->realloc == NULL && allocator->free == NULL;
}

static int bb_allocator_is_valid(const bb_allocator *allocator) {
  return allocator->alloc != NULL && allocator->realloc != NULL && allocator->free != NULL;
}

static int bb_utf8_policy_is_valid(bb_utf8_policy policy) {
  return policy == BB_UTF8_REJECT_INVALID || policy == BB_UTF8_ALLOW_INVALID;
}

void bb_limits_init(bb_limits *limits) {
  if (limits == NULL) {
    return;
  }
  limits->max_total_allocation_bytes = 256u * 1024u * 1024u;
  limits->max_source_bytes = 16u * 1024u * 1024u;
  limits->max_sources = UINT32_C(1024);
  limits->max_diagnostics = UINT32_C(256);
  limits->max_related_spans = UINT32_C(32);
  limits->max_provenance_records = UINT32_C(1000000);
  limits->max_syntax_tokens = UINT32_C(1000000);
  limits->max_syntax_nodes = UINT32_C(1000000);
  limits->max_syntax_depth = UINT32_C(256);
  limits->max_collection_depth = UINT32_C(1024);
  limits->max_collection_item_values = UINT32_C(64);
  limits->max_import_depth = UINT32_C(32);
  limits->max_graph_depth = UINT32_C(1024);
  limits->max_graph_nodes = UINT64_C(1000000);
  limits->max_collection_cardinality = UINT64_C(1000000);
  limits->max_output_count = UINT64_C(100000);
  limits->max_render_pixels = UINT64_C(67108864);
}

void bb_context_desc_init(bb_context_desc *desc) {
  if (desc == NULL) {
    return;
  }
  memset(desc, 0, sizeof(*desc));
  desc->struct_size = (uint32_t)sizeof(*desc);
  bb_limits_init(&desc->limits);
  desc->utf8_policy = BB_UTF8_REJECT_INVALID;
}

bb_status bb_context_create(const bb_context_desc *desc, bb_context **out_context) {
  bb_context_desc defaults;
  const bb_context_desc *effective_desc = desc;
  bb_allocator allocator;
  bb_context *context;
  if (out_context == NULL) {
    return BB_STATUS_INVALID_ARGUMENT;
  }
  *out_context = NULL;

  if (effective_desc == NULL) {
    bb_context_desc_init(&defaults);
    effective_desc = &defaults;
  }
  if (effective_desc->struct_size != sizeof(*effective_desc) || !bb_utf8_policy_is_valid(effective_desc->utf8_policy)) {
    return BB_STATUS_INVALID_ARGUMENT;
  }
  allocator = bb_default_allocator();
  if (!bb_allocator_is_empty(&effective_desc->allocator)) {
    if (!bb_allocator_is_valid(&effective_desc->allocator)) {
      return BB_STATUS_INVALID_ARGUMENT;
    }
    allocator = effective_desc->allocator;
  }

  context = allocator.alloc(allocator.user, sizeof(*context), _Alignof(bb_context));
  if (context == NULL) {
    return BB_STATUS_OUT_OF_MEMORY;
  }
  memset(context, 0, sizeof(*context));
  context->allocator = allocator;
  context->limits = effective_desc->limits;
  context->utf8_policy = effective_desc->utf8_policy;
  context->log_user = effective_desc->log_user;
  context->log = effective_desc->log;
  *out_context = context;
  if (context->log != NULL)
    context->log(context->log_user, BB_LOG_TRACE, (bb_string_view){"context created", sizeof("context created") - 1});
  return BB_STATUS_OK;
}

void bb_context_destroy(bb_context *context) {
  bb_allocator allocator;
  size_t index;
  size_t source_array_size;
  if (context == NULL) {
    return;
  }
  if (context->log != NULL)
    context->log(context->log_user, BB_LOG_TRACE, (bb_string_view){"context destroyed", sizeof("context destroyed") - 1});
  for (index = 0; index < context->source_count; index += 1) {
    bb_context_deallocate(
      context,
      context->sources[index].storage,
      context->sources[index].storage_size,
      _Alignof(uint8_t)
    );
  }
  source_array_size = context->source_capacity * sizeof(*context->sources);
  bb_context_deallocate(context, context->sources, source_array_size, _Alignof(bb_source_record));
  allocator = context->allocator;
  allocator.free(allocator.user, context, sizeof(*context), _Alignof(bb_context));
}

bb_status bb_context_get_limits(const bb_context *context, bb_limits *out_limits) {
  if (context == NULL || out_limits == NULL) {
    return BB_STATUS_INVALID_ARGUMENT;
  }
  *out_limits = context->limits;
  return BB_STATUS_OK;
}

bb_status bb_context_allocate(bb_context *context, size_t size, size_t alignment, void **out_pointer) {
  size_t allocation_bytes;
  void *pointer;
  if (context == NULL || out_pointer == NULL || alignment == 0 || (alignment & (alignment - 1)) != 0) {
    return BB_STATUS_INVALID_ARGUMENT;
  }
  *out_pointer = NULL;
  if (size == 0) {
    return BB_STATUS_OK;
  }
  if (!bb_size_add(context->allocation_bytes, size, &allocation_bytes)) {
    return BB_STATUS_OVERFLOW;
  }
  if (allocation_bytes > context->limits.max_total_allocation_bytes) {
    return BB_STATUS_LIMIT_EXCEEDED;
  }
  pointer = context->allocator.alloc(context->allocator.user, size, alignment);
  if (pointer == NULL) {
    return BB_STATUS_OUT_OF_MEMORY;
  }
  context->allocation_bytes = allocation_bytes;
  *out_pointer = pointer;
  return BB_STATUS_OK;
}

bb_status bb_context_reallocate(
  bb_context *context,
  void *pointer,
  size_t old_size,
  size_t new_size,
  size_t alignment,
  void **out_pointer
) {
  size_t retained_bytes;
  size_t allocation_bytes;
  void *new_pointer;
  if (context == NULL || out_pointer == NULL || alignment == 0 || (alignment & (alignment - 1)) != 0 ||
      old_size > context->allocation_bytes || (pointer == NULL && old_size != 0)) {
    return BB_STATUS_INVALID_ARGUMENT;
  }
  *out_pointer = NULL;
  if (new_size == 0) {
    bb_context_deallocate(context, pointer, old_size, alignment);
    return BB_STATUS_OK;
  }
  retained_bytes = context->allocation_bytes - old_size;
  if (!bb_size_add(retained_bytes, new_size, &allocation_bytes)) {
    return BB_STATUS_OVERFLOW;
  }
  if (allocation_bytes > context->limits.max_total_allocation_bytes) {
    return BB_STATUS_LIMIT_EXCEEDED;
  }
  new_pointer = context->allocator.realloc(context->allocator.user, pointer, old_size, new_size, alignment);
  if (new_pointer == NULL) {
    return BB_STATUS_OUT_OF_MEMORY;
  }
  context->allocation_bytes = allocation_bytes;
  *out_pointer = new_pointer;
  return BB_STATUS_OK;
}

void bb_context_deallocate(bb_context *context, void *pointer, size_t size, size_t alignment) {
  if (context == NULL || pointer == NULL) {
    return;
  }
  context->allocator.free(context->allocator.user, pointer, size, alignment);
  context->allocation_bytes -= size;
}

size_t bb_context_allocation_bytes(const bb_context *context) {
  return context == NULL ? 0 : context->allocation_bytes;
}

bb_utf8_policy bb_context_utf8_policy(const bb_context *context) {
  return context == NULL ? BB_UTF8_REJECT_INVALID : context->utf8_policy;
}

static bb_status bb_context_grow_sources(bb_context *context) {
  size_t new_capacity;
  size_t old_size;
  size_t new_size;
  void *new_sources;
  bb_status status;
  if (context->source_count < context->source_capacity) {
    return BB_STATUS_OK;
  }
  new_capacity = context->source_capacity == 0 ? 4 : context->source_capacity * 2;
  if (new_capacity < context->source_capacity) {
    return BB_STATUS_OVERFLOW;
  }
  if (new_capacity > context->limits.max_sources) {
    new_capacity = context->limits.max_sources;
  }
  if (new_capacity <= context->source_capacity) {
    return BB_STATUS_LIMIT_EXCEEDED;
  }
  if (!bb_size_multiply(context->source_capacity, sizeof(*context->sources), &old_size) ||
      !bb_size_multiply(new_capacity, sizeof(*context->sources), &new_size)) {
    return BB_STATUS_OVERFLOW;
  }
  status = bb_context_reallocate(
    context,
    context->sources,
    old_size,
    new_size,
    _Alignof(bb_source_record),
    &new_sources
  );
  if (status != BB_STATUS_OK) {
    return status;
  }
  context->sources = new_sources;
  context->source_capacity = new_capacity;
  return BB_STATUS_OK;
}

bb_status bb_context_add_source(bb_context *context, bb_string_view name, bb_bytes contents, bb_source_id *out_source_id) {
  bb_source_record *record;
  size_t storage_size;
  uint8_t *storage = NULL;
  bb_status status;
  if (context == NULL || out_source_id == NULL || (name.length != 0 && name.data == NULL) ||
      (contents.length != 0 && contents.data == NULL)) {
    return BB_STATUS_INVALID_ARGUMENT;
  }
  *out_source_id = BB_SOURCE_ID_NONE;
  if (context->source_count >= context->limits.max_sources || contents.length > context->limits.max_source_bytes ||
      contents.length > UINT32_MAX) {
    return BB_STATUS_LIMIT_EXCEEDED;
  }
  if (context->utf8_policy == BB_UTF8_REJECT_INVALID) {
    status = bb_utf8_validate((bb_bytes){(const uint8_t *)name.data, name.length});
    if (status != BB_STATUS_OK) {
      return status;
    }
    status = bb_utf8_validate(contents);
    if (status != BB_STATUS_OK) {
      return status;
    }
  }
  if (!bb_size_add(name.length, contents.length, &storage_size)) {
    return BB_STATUS_OVERFLOW;
  }
  status = bb_context_grow_sources(context);
  if (status != BB_STATUS_OK) {
    return status;
  }
  status = bb_context_allocate(context, storage_size, _Alignof(uint8_t), (void **)&storage);
  if (status != BB_STATUS_OK) {
    return status;
  }
  if (name.length != 0) {
    memcpy(storage, name.data, name.length);
  }
  if (contents.length != 0) {
    memcpy(storage + name.length, contents.data, contents.length);
  }
  record = &context->sources[context->source_count];
  record->storage = storage;
  record->storage_size = storage_size;
  record->name.data = name.length == 0 ? NULL : (const char *)storage;
  record->name.length = name.length;
  record->contents.data = contents.length == 0 ? NULL : storage + name.length;
  record->contents.length = contents.length;
  context->source_count += 1;
  *out_source_id = (bb_source_id)context->source_count;
  return BB_STATUS_OK;
}

size_t bb_context_source_count(const bb_context *context) {
  return context == NULL ? 0 : context->source_count;
}

bb_status bb_context_source_info(const bb_context *context, bb_source_id source_id, bb_source_info *out_info) {
  const bb_source_record *record;
  if (context == NULL || out_info == NULL || source_id == BB_SOURCE_ID_NONE || source_id > context->source_count) {
    return context != NULL && out_info != NULL ? BB_STATUS_NOT_FOUND : BB_STATUS_INVALID_ARGUMENT;
  }
  record = &context->sources[source_id - 1];
  out_info->id = source_id;
  out_info->name = record->name;
  out_info->contents = record->contents;
  return BB_STATUS_OK;
}

bb_status bb_context_validate_span(const bb_context *context, bb_span span) {
  const bb_source_record *source;
  if (context == NULL) {
    return BB_STATUS_INVALID_ARGUMENT;
  }
  if (span.source_id == BB_SOURCE_ID_NONE) {
    return span.byte_start == 0 && span.byte_end == 0 ? BB_STATUS_OK : BB_STATUS_INVALID_ARGUMENT;
  }
  if (span.source_id > context->source_count) {
    return BB_STATUS_NOT_FOUND;
  }
  source = &context->sources[span.source_id - 1];
  if (span.byte_start > span.byte_end || span.byte_end > source->contents.length) {
    return BB_STATUS_INVALID_ARGUMENT;
  }
  return BB_STATUS_OK;
}

const char *bb_status_name(bb_status status) {
  switch (status) {
    case BB_STATUS_OK:
      return "ok";
    case BB_STATUS_INVALID_ARGUMENT:
      return "invalid argument";
    case BB_STATUS_OUT_OF_MEMORY:
      return "out of memory";
    case BB_STATUS_OVERFLOW:
      return "overflow";
    case BB_STATUS_LIMIT_EXCEEDED:
      return "limit exceeded";
    case BB_STATUS_UNSUPPORTED:
      return "unsupported";
    case BB_STATUS_INTERNAL_ERROR:
      return "internal error";
    case BB_STATUS_INVALID_UTF8:
      return "invalid UTF-8";
    case BB_STATUS_NOT_FOUND:
      return "not found";
    case BB_STATUS_CANCELLED:
      return "cancelled";
    default:
      return "unknown status";
  }
}
