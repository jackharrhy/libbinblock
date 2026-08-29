#include <binblock/module.h>

#include <string.h>

static uint64_t bb_module_hash(bb_bytes bytes) {
  uint64_t hash = UINT64_C(14695981039346656037);
  size_t index;
  for (index = 0; index < bytes.length; index += 1)
    hash = (hash ^ bytes.data[index]) * UINT64_C(1099511628211);
  return hash;
}

static void bb_module_write_u16(uint8_t *destination, uint16_t value) {
  destination[0] = (uint8_t)(value >> 8);
  destination[1] = (uint8_t)value;
}

static void bb_module_write_u64(uint8_t *destination, uint64_t value) {
  uint32_t index;
  for (index = 0; index < 8; index += 1)
    destination[index] = (uint8_t)(value >> (56 - index * 8));
}

static uint16_t bb_module_read_u16(const uint8_t *source) {
  return (uint16_t)((uint16_t)source[0] << 8) | source[1];
}

static uint64_t bb_module_read_u64(const uint8_t *source) {
  uint64_t value = 0;
  uint32_t index;
  for (index = 0; index < 8; index += 1) value = (value << 8) | source[index];
  return value;
}

bb_status bb_precompiled_module_measure(bb_bytes source, size_t *out_size) {
  if (out_size == NULL || (source.length != 0 && source.data == NULL)) return BB_STATUS_INVALID_ARGUMENT;
  if (bb_utf8_validate(source) != BB_STATUS_OK) return BB_STATUS_INVALID_UTF8;
  if (source.length > SIZE_MAX - BB_PRECOMPILED_MODULE_HEADER_SIZE) return BB_STATUS_OVERFLOW;
  *out_size = BB_PRECOMPILED_MODULE_HEADER_SIZE + source.length;
  return BB_STATUS_OK;
}

bb_status bb_precompiled_module_write(
  bb_bytes source,
  uint8_t *destination,
  size_t capacity,
  size_t *out_size
) {
  size_t required;
  bb_status status = bb_precompiled_module_measure(source, &required);
  if (status != BB_STATUS_OK) return status;
  if (out_size == NULL || destination == NULL || capacity < required) return BB_STATUS_INVALID_ARGUMENT;
  destination[0] = 'B';
  destination[1] = 'B';
  destination[2] = 'M';
  destination[3] = 0;
  bb_module_write_u16(destination + 4, BB_PRECOMPILED_MODULE_VERSION);
  bb_module_write_u16(destination + 6, 0);
  bb_module_write_u64(destination + 8, source.length);
  bb_module_write_u64(destination + 16, bb_module_hash(source));
  if (source.length != 0) memcpy(destination + BB_PRECOMPILED_MODULE_HEADER_SIZE, source.data, source.length);
  *out_size = required;
  return BB_STATUS_OK;
}

bb_status bb_precompiled_module_read(
  bb_bytes encoded,
  bb_precompiled_module_info *out_info,
  bb_bytes *out_source
) {
  uint16_t version;
  uint16_t flags;
  uint64_t source_length;
  uint64_t source_hash;
  bb_bytes source;
  if (out_info == NULL || out_source == NULL || encoded.data == NULL ||
      encoded.length < BB_PRECOMPILED_MODULE_HEADER_SIZE) return BB_STATUS_INVALID_ARGUMENT;
  memset(out_info, 0, sizeof(*out_info));
  *out_source = (bb_bytes){NULL, 0};
  if (encoded.data[0] != 'B' || encoded.data[1] != 'B' || encoded.data[2] != 'M' || encoded.data[3] != 0)
    return BB_STATUS_INVALID_ARGUMENT;
  version = bb_module_read_u16(encoded.data + 4);
  flags = bb_module_read_u16(encoded.data + 6);
  source_length = bb_module_read_u64(encoded.data + 8);
  source_hash = bb_module_read_u64(encoded.data + 16);
  if (version != BB_PRECOMPILED_MODULE_VERSION) return BB_STATUS_UNSUPPORTED;
  if (flags != 0 || source_length > SIZE_MAX || source_length != encoded.length - BB_PRECOMPILED_MODULE_HEADER_SIZE)
    return BB_STATUS_INVALID_ARGUMENT;
  source = (bb_bytes){encoded.data + BB_PRECOMPILED_MODULE_HEADER_SIZE, (size_t)source_length};
  if (bb_module_hash(source) != source_hash) return BB_STATUS_INVALID_ARGUMENT;
  if (bb_utf8_validate(source) != BB_STATUS_OK) return BB_STATUS_INVALID_UTF8;
  *out_info = (bb_precompiled_module_info){version, flags, source_length, source_hash};
  *out_source = source;
  return BB_STATUS_OK;
}
