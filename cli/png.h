#ifndef BINBLOCK_CLI_PNG_H
#define BINBLOCK_CLI_PNG_H

#include <binblock/raster.h>

typedef struct bb_cli_png {
  uint32_t width;
  uint32_t height;
  uint8_t *rgba;
  size_t rgba_length;
} bb_cli_png;

int bb_cli_png_info(const uint8_t *bytes, size_t length, uint32_t *out_width, uint32_t *out_height);
int bb_cli_png_decode(const uint8_t *bytes, size_t length, bb_cli_png *out_png);
void bb_cli_png_destroy(bb_cli_png *png);
int bb_cli_png_write_surface(const char *path, const bb_surface *surface);

#endif
