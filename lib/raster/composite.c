#include <binblock/raster.h>

#include "raster_internal.h"

#include <string.h>

bb_status bb_raster_source_over(
  bb_context *context,
  const bb_surface *destination,
  const bb_surface *source,
  int32_t offset_x,
  int32_t offset_y,
  double opacity,
  bb_surface **out_surface
) {
  bb_surface *output;
  uint32_t source_y;
  bb_status status;
  if (out_surface == NULL) {
    return BB_STATUS_INVALID_ARGUMENT;
  }
  *out_surface = NULL;
  if (context == NULL || destination == NULL || source == NULL || !bb_raster_unit_value_is_valid(opacity)) {
    return BB_STATUS_INVALID_ARGUMENT;
  }
  status = bb_surface_clone(context, destination, &output);
  if (status != BB_STATUS_OK) {
    return status;
  }
  for (source_y = 0; source_y < source->desc.height; source_y += 1) {
    const int64_t destination_y = (int64_t)source_y + offset_y;
    uint32_t source_x;
    if (destination_y < 0 || destination_y >= destination->desc.height) {
      continue;
    }
    for (source_x = 0; source_x < source->desc.width; source_x += 1) {
      const int64_t destination_x = (int64_t)source_x + offset_x;
      const uint8_t *source_pixel;
      uint8_t *destination_pixel;
      double source_alpha;
      double destination_alpha;
      double output_alpha;
      uint8_t channel;
      if (destination_x < 0 || destination_x >= destination->desc.width) {
        continue;
      }
      source_pixel = source->pixels + (size_t)source_y * source->desc.row_pitch + (size_t)source_x * 4;
      destination_pixel = output->pixels + (size_t)destination_y * output->desc.row_pitch + (size_t)destination_x * 4;
      source_alpha = ((double)source_pixel[3] / 255.0) * opacity;
      destination_alpha = (double)destination_pixel[3] / 255.0;
      output_alpha = source_alpha + destination_alpha * (1.0 - source_alpha);
      if (output_alpha == 0.0) {
        memset(destination_pixel, 0, 4);
        continue;
      }
      for (channel = 0; channel < 3; channel += 1) {
        destination_pixel[channel] = bb_raster_round_u8(
          (source_pixel[channel] * source_alpha +
           destination_pixel[channel] * destination_alpha * (1.0 - source_alpha)) /
          output_alpha
        );
      }
      destination_pixel[3] = bb_raster_round_u8(output_alpha * 255.0);
    }
  }
  *out_surface = output;
  return BB_STATUS_OK;
}

bb_status bb_raster_canvas(
  bb_context *context,
  const bb_surface *source,
  uint32_t width,
  uint32_t height,
  int32_t x,
  int32_t y,
  bb_surface **out_surface
) {
  bb_surface *clear;
  bb_status status;
  if (out_surface == NULL) {
    return BB_STATUS_INVALID_ARGUMENT;
  }
  *out_surface = NULL;
  status = bb_raster_fill(context, width, height, (bb_rgba8){0, 0, 0, 0}, &clear);
  if (status != BB_STATUS_OK) {
    return status;
  }
  status = bb_raster_source_over(context, clear, source, x, y, 1.0, out_surface);
  bb_surface_destroy(clear);
  return status;
}

bb_status bb_raster_apply_mask(
  bb_context *context,
  const bb_surface *source,
  const bb_surface *mask,
  bb_mask_mode mode,
  bb_surface **out_surface
) {
  bb_surface *output;
  size_t offset;
  bb_status status;
  if (out_surface == NULL) {
    return BB_STATUS_INVALID_ARGUMENT;
  }
  *out_surface = NULL;
  if (context == NULL || source == NULL || mask == NULL || (mode != BB_MASK_MULTIPLY && mode != BB_MASK_REPLACE) ||
      source->desc.width != mask->desc.width || source->desc.height != mask->desc.height) {
    return BB_STATUS_INVALID_ARGUMENT;
  }
  status = bb_surface_clone(context, source, &output);
  if (status != BB_STATUS_OK) {
    return status;
  }
  for (offset = 3; offset < output->pixel_bytes; offset += 4) {
    output->pixels[offset] = mode == BB_MASK_REPLACE
                               ? mask->pixels[offset]
                               : bb_raster_round_u8(((double)output->pixels[offset] * mask->pixels[offset]) / 255.0);
  }
  *out_surface = output;
  return BB_STATUS_OK;
}
