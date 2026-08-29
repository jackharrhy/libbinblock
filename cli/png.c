#include "png.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

static uint32_t bb_cli_png_u32(const uint8_t *bytes) {
  return ((uint32_t)bytes[0] << 24) | ((uint32_t)bytes[1] << 16) | ((uint32_t)bytes[2] << 8) | bytes[3];
}

static int bb_cli_size_multiply(size_t left, size_t right, size_t *out_result) {
  if (out_result == NULL || (left != 0 && right > SIZE_MAX / left)) return 0;
  *out_result = left * right;
  return 1;
}

static void bb_cli_png_store_u32(uint8_t *bytes, uint32_t value) {
  bytes[0] = (uint8_t)(value >> 24);
  bytes[1] = (uint8_t)(value >> 16);
  bytes[2] = (uint8_t)(value >> 8);
  bytes[3] = (uint8_t)value;
}

static uint8_t bb_cli_png_paeth(uint8_t left, uint8_t above, uint8_t upper_left) {
  const int prediction = (int)left + above - upper_left;
  const int left_distance = abs(prediction - left);
  const int above_distance = abs(prediction - above);
  const int upper_left_distance = abs(prediction - upper_left);
  if (left_distance <= above_distance && left_distance <= upper_left_distance) return left;
  return above_distance <= upper_left_distance ? above : upper_left;
}

int bb_cli_png_info(const uint8_t *bytes, size_t length, uint32_t *out_width, uint32_t *out_height) {
  static const uint8_t signature[8] = {137, 80, 78, 71, 13, 10, 26, 10};
  const uint8_t *data;
  if (bytes == NULL || out_width == NULL || out_height == NULL || length < 33 ||
      memcmp(bytes, signature, sizeof(signature)) != 0 || bb_cli_png_u32(bytes + 8) != 13 ||
      memcmp(bytes + 12, "IHDR", 4) != 0) return 0;
  data = bytes + 16;
  *out_width = bb_cli_png_u32(data);
  *out_height = bb_cli_png_u32(data + 4);
  return *out_width != 0 && *out_height != 0 && data[8] == 8 && (data[9] == 2 || data[9] == 6) &&
         data[10] == 0 && data[11] == 0 && data[12] == 0;
}

int bb_cli_png_decode(const uint8_t *bytes, size_t length, bb_cli_png *out_png) {
  size_t offset = 8;
  uint32_t width = 0;
  uint32_t height = 0;
  uint8_t channels = 0;
  uint8_t *compressed = NULL;
  size_t compressed_length = 0;
  uint8_t *filtered = NULL;
  uint8_t *reconstructed = NULL;
  uint8_t *rgba = NULL;
  size_t row_bytes;
  size_t filtered_length;
  size_t reconstructed_length;
  size_t rgba_length;
  size_t rgba_row_bytes;
  uLongf zlib_length;
  uint32_t y;
  if (out_png == NULL || !bb_cli_png_info(bytes, length, &width, &height)) return 0;
  memset(out_png, 0, sizeof(*out_png));
  while (offset + 12 <= length) {
    const uint32_t chunk_length = bb_cli_png_u32(bytes + offset);
    const uint8_t *type = bytes + offset + 4;
    const size_t data_offset = offset + 8;
    uint8_t *replacement;
    if (chunk_length > length - data_offset - 4) goto fail;
    if (memcmp(type, "IHDR", 4) == 0) channels = bytes[data_offset + 9] == 2 ? 3 : 4;
    else if (memcmp(type, "IDAT", 4) == 0) {
      if (chunk_length > SIZE_MAX - compressed_length) goto fail;
      replacement = realloc(compressed, compressed_length + chunk_length);
      if (replacement == NULL) goto fail;
      compressed = replacement;
      memcpy(compressed + compressed_length, bytes + data_offset, chunk_length);
      compressed_length += chunk_length;
    } else if (memcmp(type, "IEND", 4) == 0) break;
    offset = data_offset + chunk_length + 4;
  }
  if (channels == 0 || compressed_length == 0 || width > SIZE_MAX / channels) goto fail;
  row_bytes = (size_t)width * channels;
  if (row_bytes == SIZE_MAX || height > SIZE_MAX / (row_bytes + 1)) goto fail;
  filtered_length = (row_bytes + 1) * height;
  if (height > SIZE_MAX / row_bytes || !bb_cli_size_multiply(width, 4, &rgba_row_bytes)) goto fail;
  reconstructed_length = row_bytes * height;
  if (!bb_cli_size_multiply(rgba_row_bytes, height, &rgba_length)) goto fail;
  if (filtered_length > ULONG_MAX || compressed_length > ULONG_MAX) goto fail;
  filtered = malloc(filtered_length);
  reconstructed = malloc(reconstructed_length);
  rgba = malloc(rgba_length);
  if (filtered == NULL || reconstructed == NULL || rgba == NULL) goto fail;
  zlib_length = (uLongf)filtered_length;
  if (uncompress(filtered, &zlib_length, compressed, (uLong)compressed_length) != Z_OK ||
      zlib_length != filtered_length) goto fail;
  offset = 0;
  for (y = 0; y < height; y += 1) {
    const uint8_t filter = filtered[offset++];
    const size_t row_offset = (size_t)y * row_bytes;
    size_t x;
    if (filter > 4) goto fail;
    for (x = 0; x < row_bytes; x += 1) {
      const uint8_t value = filtered[offset++];
      const uint8_t left = x >= channels ? reconstructed[row_offset + x - channels] : 0;
      const uint8_t above = y > 0 ? reconstructed[row_offset - row_bytes + x] : 0;
      const uint8_t upper_left = y > 0 && x >= channels ? reconstructed[row_offset - row_bytes + x - channels] : 0;
      uint8_t predictor = 0;
      if (filter == 1) predictor = left;
      else if (filter == 2) predictor = above;
      else if (filter == 3) predictor = (uint8_t)(((uint16_t)left + above) / 2);
      else if (filter == 4) predictor = bb_cli_png_paeth(left, above, upper_left);
      reconstructed[row_offset + x] = (uint8_t)(value + predictor);
    }
  }
  for (offset = 0; offset < (size_t)width * height; offset += 1) {
    rgba[offset * 4] = reconstructed[offset * channels];
    rgba[offset * 4 + 1] = reconstructed[offset * channels + 1];
    rgba[offset * 4 + 2] = reconstructed[offset * channels + 2];
    rgba[offset * 4 + 3] = channels == 4 ? reconstructed[offset * channels + 3] : 255;
  }
  out_png->width = width;
  out_png->height = height;
  out_png->rgba = rgba;
  out_png->rgba_length = rgba_length;
  free(compressed);
  free(filtered);
  free(reconstructed);
  return 1;

fail:
  free(compressed);
  free(filtered);
  free(reconstructed);
  free(rgba);
  return 0;
}

void bb_cli_png_destroy(bb_cli_png *png) {
  if (png == NULL) return;
  free(png->rgba);
  memset(png, 0, sizeof(*png));
}

static int bb_cli_png_write_chunk(FILE *file, const char type[4], const uint8_t *data, uint32_t length) {
  uint8_t header[8];
  uint8_t crc_bytes[4];
  uLong crc;
  bb_cli_png_store_u32(header, length);
  memcpy(header + 4, type, 4);
  if (fwrite(header, 1, sizeof(header), file) != sizeof(header)) return 0;
  if (length != 0 && fwrite(data, 1, length, file) != length) return 0;
  crc = crc32(0, (const Bytef *)type, 4);
  if (length != 0) crc = crc32(crc, data, length);
  bb_cli_png_store_u32(crc_bytes, (uint32_t)crc);
  return fwrite(crc_bytes, 1, sizeof(crc_bytes), file) == sizeof(crc_bytes);
}

int bb_cli_png_write_surface(const char *path, const bb_surface *surface) {
  static const uint8_t signature[8] = {137, 80, 78, 71, 13, 10, 26, 10};
  bb_const_image_view view;
  uint8_t ihdr[13] = {0};
  uint8_t *raw = NULL;
  uint8_t *compressed = NULL;
  size_t row_bytes;
  size_t raw_length;
  uLongf compressed_length;
  uint32_t y;
  FILE *file = NULL;
  int ok = 0;
  if (path == NULL || bb_surface_get_const_view(surface, &view) != BB_STATUS_OK ||
      !bb_cli_size_multiply(view.desc.width, 4, &row_bytes)) return 0;
  if (row_bytes == SIZE_MAX || view.desc.height > SIZE_MAX / (row_bytes + 1)) return 0;
  raw_length = (row_bytes + 1) * view.desc.height;
  if (raw_length > ULONG_MAX) return 0;
  raw = malloc(raw_length);
  if (raw == NULL) goto cleanup;
  for (y = 0; y < view.desc.height; y += 1) {
    raw[(size_t)y * (row_bytes + 1)] = 0;
    memcpy(
      raw + (size_t)y * (row_bytes + 1) + 1,
      view.data + (size_t)y * view.desc.row_pitch,
      row_bytes
    );
  }
  compressed_length = compressBound((uLong)raw_length);
  compressed = malloc((size_t)compressed_length);
  if (compressed == NULL || compress2(compressed, &compressed_length, raw, (uLong)raw_length, Z_BEST_COMPRESSION) != Z_OK ||
      compressed_length > UINT32_MAX) goto cleanup;
  file = fopen(path, "wb");
  if (file == NULL || fwrite(signature, 1, sizeof(signature), file) != sizeof(signature)) goto cleanup;
  bb_cli_png_store_u32(ihdr, view.desc.width);
  bb_cli_png_store_u32(ihdr + 4, view.desc.height);
  ihdr[8] = 8;
  ihdr[9] = 6;
  if (!bb_cli_png_write_chunk(file, "IHDR", ihdr, sizeof(ihdr)) ||
      !bb_cli_png_write_chunk(file, "IDAT", compressed, (uint32_t)compressed_length) ||
      !bb_cli_png_write_chunk(file, "IEND", NULL, 0)) goto cleanup;
  ok = 1;

cleanup:
  if (file != NULL && fclose(file) != 0) ok = 0;
  free(compressed);
  free(raw);
  return ok;
}
