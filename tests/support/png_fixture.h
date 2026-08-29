#ifndef BINBLOCK_PNG_FIXTURE_H
#define BINBLOCK_PNG_FIXTURE_H

#include <stddef.h>
#include <stdint.h>

typedef struct bb_png_fixture {
  uint32_t width;
  uint32_t height;
  uint8_t *rgba;
  size_t rgba_length;
} bb_png_fixture;

int bb_png_fixture_load(const char *relative_path, bb_png_fixture *out_fixture);
void bb_png_fixture_destroy(bb_png_fixture *fixture);

#endif
