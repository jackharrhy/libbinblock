#ifndef BINBLOCK_RASTER_INTERNAL_H
#define BINBLOCK_RASTER_INTERNAL_H

#include <binblock/raster.h>

struct bb_surface {
  bb_context *context;
  bb_image_desc desc;
  uint8_t *pixels;
  size_t pixel_bytes;
};

bb_status bb_surface_allocate(bb_context *context, uint32_t width, uint32_t height, bb_surface **out_surface);
bb_status bb_surface_clone(bb_context *context, const bb_surface *source, bb_surface **out_surface);
uint8_t bb_raster_round_u8(double value);
uint8_t bb_raster_clamp_even_u8(double value);
int bb_raster_unit_value_is_valid(double value);

#endif
