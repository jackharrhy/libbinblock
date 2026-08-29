#ifndef BINBLOCK_MODULE_H
#define BINBLOCK_MODULE_H

#include <binblock/binblock.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BB_PRECOMPILED_MODULE_VERSION 1u
#define BB_PRECOMPILED_MODULE_HEADER_SIZE 24u

typedef struct bb_precompiled_module_info {
  uint32_t version;
  uint32_t flags;
  uint64_t source_length;
  uint64_t source_hash;
} bb_precompiled_module_info;

/* The v1 portable module envelope stores validated UTF-8 BinScript source. All
 * integers are big-endian and the envelope is never a native struct dump. */
BB_API bb_status bb_precompiled_module_measure(bb_bytes source, size_t *out_size);
BB_API bb_status bb_precompiled_module_write(
  bb_bytes source,
  uint8_t *destination,
  size_t capacity,
  size_t *out_size
);
BB_API bb_status bb_precompiled_module_read(
  bb_bytes encoded,
  bb_precompiled_module_info *out_info,
  bb_bytes *out_source
);

#ifdef __cplusplus
}
#endif

#endif
