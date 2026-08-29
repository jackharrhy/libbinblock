#include "png_fixture.h"

#include "checked_math.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

static uint32_t bb_png_u32(const uint8_t *bytes) {
  return ((uint32_t)bytes[0] << 24) | ((uint32_t)bytes[1] << 16) | ((uint32_t)bytes[2] << 8) | bytes[3];
}

static uint8_t bb_png_paeth(uint8_t left, uint8_t above, uint8_t upper_left) {
  const int prediction = (int)left + above - upper_left;
  const int left_distance = abs(prediction - left);
  const int above_distance = abs(prediction - above);
  const int upper_left_distance = abs(prediction - upper_left);
  if (left_distance <= above_distance && left_distance <= upper_left_distance) return left;
  return above_distance <= upper_left_distance ? above : upper_left;
}

static int bb_png_read_file(const char *relative_path, uint8_t **out_bytes, size_t *out_length) {
  char path[4096];
  FILE *file;
  long length;
  uint8_t *bytes;
  if (snprintf(path, sizeof(path), "%s/%s", BB_TEST_SOURCE_DIR, relative_path) < 0) return 0;
  file = fopen(path, "rb");
  if (file == NULL) return 0;
  if (fseek(file, 0, SEEK_END) != 0) {
    fclose(file);
    return 0;
  }
  length = ftell(file);
  if (length < 0 || fseek(file, 0, SEEK_SET) != 0) {
    fclose(file);
    return 0;
  }
  bytes = malloc((size_t)length);
  if (bytes == NULL || fread(bytes, 1, (size_t)length, file) != (size_t)length) {
    free(bytes);
    fclose(file);
    return 0;
  }
  fclose(file);
  *out_bytes = bytes;
  *out_length = (size_t)length;
  return 1;
}

int bb_png_fixture_load(const char *relative_path, bb_png_fixture *out_fixture) {
  static const uint8_t signature[8] = {137, 80, 78, 71, 13, 10, 26, 10};
  uint8_t *file_bytes = NULL;
  size_t file_length = 0;
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
  if (relative_path == NULL || out_fixture == NULL) return 0;
  memset(out_fixture, 0, sizeof(*out_fixture));
  if (!bb_png_read_file(relative_path, &file_bytes, &file_length) || file_length < 8 ||
      memcmp(file_bytes, signature, sizeof(signature)) != 0) {
    free(file_bytes);
    return 0;
  }
  while (offset + 12 <= file_length) {
    const uint32_t chunk_length = bb_png_u32(file_bytes + offset);
    const uint8_t *type = file_bytes + offset + 4;
    const size_t data_offset = offset + 8;
    uint8_t *new_compressed;
    if (chunk_length > file_length - data_offset - 4) goto fail;
    if (memcmp(type, "IHDR", 4) == 0) {
      const uint8_t *data = file_bytes + data_offset;
      if (chunk_length != 13) goto fail;
      width = bb_png_u32(data);
      height = bb_png_u32(data + 4);
      if (width == 0 || height == 0 || data[8] != 8 || (data[9] != 2 && data[9] != 6) || data[10] != 0 ||
          data[11] != 0 || data[12] != 0) {
        goto fail;
      }
      channels = data[9] == 2 ? 3 : 4;
    } else if (memcmp(type, "IDAT", 4) == 0) {
      if (chunk_length > SIZE_MAX - compressed_length) goto fail;
      new_compressed = realloc(compressed, compressed_length + chunk_length);
      if (new_compressed == NULL) goto fail;
      compressed = new_compressed;
      memcpy(compressed + compressed_length, file_bytes + data_offset, chunk_length);
      compressed_length += chunk_length;
    } else if (memcmp(type, "IEND", 4) == 0) {
      break;
    }
    offset = data_offset + chunk_length + 4;
  }
  if (channels == 0 || compressed_length == 0 || width > SIZE_MAX / channels) goto fail;
  row_bytes = (size_t)width * channels;
  if (row_bytes == SIZE_MAX || height > SIZE_MAX / (row_bytes + 1)) goto fail;
  filtered_length = (row_bytes + 1) * height;
  if (height > SIZE_MAX / row_bytes) goto fail;
  reconstructed_length = row_bytes * height;
  if (!bb_size_multiply(width, 4, &rgba_row_bytes) || !bb_size_multiply(rgba_row_bytes, height, &rgba_length)) goto fail;
  if (filtered_length > ULONG_MAX || compressed_length > ULONG_MAX) goto fail;
  filtered = malloc(filtered_length);
  reconstructed = malloc(reconstructed_length);
  rgba = malloc(rgba_length);
  if (filtered == NULL || reconstructed == NULL || rgba == NULL) goto fail;
  zlib_length = (uLongf)filtered_length;
  if (uncompress(filtered, &zlib_length, compressed, (uLong)compressed_length) != Z_OK || zlib_length != filtered_length) goto fail;
  offset = 0;
  for (y = 0; y < height; y += 1) {
    const uint8_t filter = filtered[offset++];
    size_t x;
    const size_t row_offset = (size_t)y * row_bytes;
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
      else if (filter == 4) predictor = bb_png_paeth(left, above, upper_left);
      reconstructed[row_offset + x] = (uint8_t)(value + predictor);
    }
  }
  for (offset = 0; offset < (size_t)width * height; offset += 1) {
    rgba[offset * 4] = reconstructed[offset * channels];
    rgba[offset * 4 + 1] = reconstructed[offset * channels + 1];
    rgba[offset * 4 + 2] = reconstructed[offset * channels + 2];
    rgba[offset * 4 + 3] = channels == 4 ? reconstructed[offset * channels + 3] : 255;
  }
  out_fixture->width = width;
  out_fixture->height = height;
  out_fixture->rgba = rgba;
  out_fixture->rgba_length = rgba_length;
  free(file_bytes);
  free(compressed);
  free(filtered);
  free(reconstructed);
  return 1;

fail:
  free(file_bytes);
  free(compressed);
  free(filtered);
  free(reconstructed);
  free(rgba);
  return 0;
}

void bb_png_fixture_destroy(bb_png_fixture *fixture) {
  if (fixture == NULL) return;
  free(fixture->rgba);
  memset(fixture, 0, sizeof(*fixture));
}
