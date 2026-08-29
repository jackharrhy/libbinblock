#include <binblock/raster.h>

#include "raster_internal.h"

#include <string.h>

bb_status bb_raster_crop(
  bb_context *context,
  const bb_surface *source,
  int32_t x,
  int32_t y,
  uint32_t width,
  uint32_t height,
  bb_surface **out_surface
) {
  bb_surface *output;
  uint32_t output_y;
  bb_status status;
  if (out_surface == NULL) {
    return BB_STATUS_INVALID_ARGUMENT;
  }
  *out_surface = NULL;
  if (context == NULL || source == NULL) {
    return BB_STATUS_INVALID_ARGUMENT;
  }
  status = bb_surface_allocate(context, width, height, &output);
  if (status != BB_STATUS_OK) {
    return status;
  }
  for (output_y = 0; output_y < height; output_y += 1) {
    const int64_t source_y = (int64_t)y + output_y;
    uint32_t output_x;
    if (source_y < 0 || source_y >= source->desc.height) {
      continue;
    }
    for (output_x = 0; output_x < width; output_x += 1) {
      const int64_t source_x = (int64_t)x + output_x;
      const uint8_t *source_pixel;
      uint8_t *output_pixel;
      if (source_x < 0 || source_x >= source->desc.width) {
        continue;
      }
      source_pixel = source->pixels + (size_t)source_y * source->desc.row_pitch + (size_t)source_x * 4;
      output_pixel = output->pixels + (size_t)output_y * output->desc.row_pitch + (size_t)output_x * 4;
      memcpy(output_pixel, source_pixel, 4);
    }
  }
  *out_surface = output;
  return BB_STATUS_OK;
}

bb_status bb_raster_rotate_quarter_turns(
  bb_context *context,
  const bb_surface *source,
  int32_t turns,
  bb_surface **out_surface
) {
  bb_surface *output;
  int32_t normalized_turns;
  uint32_t output_width;
  uint32_t output_height;
  uint32_t source_y;
  bb_status status;
  if (out_surface == NULL) {
    return BB_STATUS_INVALID_ARGUMENT;
  }
  *out_surface = NULL;
  if (context == NULL || source == NULL) {
    return BB_STATUS_INVALID_ARGUMENT;
  }
  normalized_turns = ((turns % 4) + 4) % 4;
  output_width = normalized_turns % 2 == 0 ? source->desc.width : source->desc.height;
  output_height = normalized_turns % 2 == 0 ? source->desc.height : source->desc.width;
  status = bb_surface_allocate(context, output_width, output_height, &output);
  if (status != BB_STATUS_OK) {
    return status;
  }
  for (source_y = 0; source_y < source->desc.height; source_y += 1) {
    uint32_t source_x;
    for (source_x = 0; source_x < source->desc.width; source_x += 1) {
      uint32_t destination_x;
      uint32_t destination_y;
      const uint8_t *source_pixel = source->pixels + (size_t)source_y * source->desc.row_pitch + (size_t)source_x * 4;
      uint8_t *destination_pixel;
      if (normalized_turns == 1) {
        destination_x = source->desc.height - 1 - source_y;
        destination_y = source_x;
      } else if (normalized_turns == 2) {
        destination_x = source->desc.width - 1 - source_x;
        destination_y = source->desc.height - 1 - source_y;
      } else if (normalized_turns == 3) {
        destination_x = source_y;
        destination_y = source->desc.width - 1 - source_x;
      } else {
        destination_x = source_x;
        destination_y = source_y;
      }
      destination_pixel = output->pixels + (size_t)destination_y * output->desc.row_pitch + (size_t)destination_x * 4;
      memcpy(destination_pixel, source_pixel, 4);
    }
  }
  *out_surface = output;
  return BB_STATUS_OK;
}

bb_status bb_raster_opacity(
  bb_context *context,
  const bb_surface *source,
  double opacity,
  bb_surface **out_surface
) {
  bb_surface *output;
  size_t offset;
  bb_status status;
  if (out_surface == NULL) {
    return BB_STATUS_INVALID_ARGUMENT;
  }
  *out_surface = NULL;
  if (!bb_raster_unit_value_is_valid(opacity)) {
    return BB_STATUS_INVALID_ARGUMENT;
  }
  status = bb_surface_clone(context, source, &output);
  if (status != BB_STATUS_OK) {
    return status;
  }
  for (offset = 3; offset < output->pixel_bytes; offset += 4) {
    output->pixels[offset] = bb_raster_round_u8((double)output->pixels[offset] * opacity);
  }
  *out_surface = output;
  return BB_STATUS_OK;
}

bb_status bb_raster_invert_alpha(bb_context *context, const bb_surface *source, bb_surface **out_surface) {
  bb_surface *output;
  size_t offset;
  bb_status status = bb_surface_clone(context, source, &output);
  if (status != BB_STATUS_OK) {
    return status;
  }
  for (offset = 3; offset < output->pixel_bytes; offset += 4) {
    output->pixels[offset] = UINT8_MAX - output->pixels[offset];
  }
  *out_surface = output;
  return BB_STATUS_OK;
}

bb_status bb_raster_set_visible_rgb(
  bb_context *context,
  const bb_surface *source,
  bb_rgba8 color,
  bb_surface **out_surface
) {
  bb_surface *output;
  size_t offset;
  bb_status status = bb_surface_clone(context, source, &output);
  if (status != BB_STATUS_OK) {
    return status;
  }
  for (offset = 0; offset < output->pixel_bytes; offset += 4) {
    if (output->pixels[offset + 3] != 0) {
      output->pixels[offset] = color.red;
      output->pixels[offset + 1] = color.green;
      output->pixels[offset + 2] = color.blue;
    }
  }
  *out_surface = output;
  return BB_STATUS_OK;
}

bb_status bb_raster_tint_chroma(
  bb_context *context,
  const bb_surface *source,
  bb_rgba8 color,
  bb_surface **out_surface
) {
  const uint8_t target[3] = {color.red, color.green, color.blue};
  bb_surface *output;
  size_t offset;
  bb_status status = bb_surface_clone(context, source, &output);
  if (status != BB_STATUS_OK) {
    return status;
  }
  for (offset = 0; offset < output->pixel_bytes; offset += 4) {
    uint8_t minimum = source->pixels[offset];
    uint8_t maximum = source->pixels[offset];
    uint8_t channel;
    for (channel = 1; channel < 3; channel += 1) {
      if (source->pixels[offset + channel] < minimum) minimum = source->pixels[offset + channel];
      if (source->pixels[offset + channel] > maximum) maximum = source->pixels[offset + channel];
    }
    for (channel = 0; channel < 3; channel += 1) {
      output->pixels[offset + channel] = bb_raster_clamp_even_u8(
        (double)minimum + ((double)(maximum - minimum) * target[channel]) / 255.0
      );
    }
  }
  *out_surface = output;
  return BB_STATUS_OK;
}

bb_status bb_raster_remap_two_color(
  bb_context *context,
  const bb_surface *source,
  bb_rgba8 source_foreground,
  bb_rgba8 source_background,
  bb_rgba8 foreground,
  bb_rgba8 background,
  bb_surface **out_surface
) {
  const uint8_t source_fg[3] = {source_foreground.red, source_foreground.green, source_foreground.blue};
  const uint8_t source_bg[3] = {source_background.red, source_background.green, source_background.blue};
  const uint8_t target_fg[3] = {foreground.red, foreground.green, foreground.blue};
  const uint8_t target_bg[3] = {background.red, background.green, background.blue};
  bb_surface *output;
  double denominator = 0.0;
  size_t offset;
  uint8_t channel;
  bb_status status;
  if (out_surface == NULL) {
    return BB_STATUS_INVALID_ARGUMENT;
  }
  *out_surface = NULL;
  for (channel = 0; channel < 3; channel += 1) {
    const double delta = (double)source_fg[channel] - source_bg[channel];
    denominator += delta * delta;
  }
  if (denominator == 0.0) {
    return BB_STATUS_INVALID_ARGUMENT;
  }
  status = bb_surface_clone(context, source, &output);
  if (status != BB_STATUS_OK) {
    return status;
  }
  for (offset = 0; offset < output->pixel_bytes; offset += 4) {
    double amount = 0.0;
    for (channel = 0; channel < 3; channel += 1) {
      amount += ((double)source->pixels[offset + channel] - source_bg[channel]) *
                ((double)source_fg[channel] - source_bg[channel]);
    }
    amount /= denominator;
    if (amount < 0.0) amount = 0.0;
    if (amount > 1.0) amount = 1.0;
    for (channel = 0; channel < 3; channel += 1) {
      output->pixels[offset + channel] = bb_raster_clamp_even_u8(
        target_bg[channel] * (1.0 - amount) + target_fg[channel] * amount
      );
    }
  }
  *out_surface = output;
  return BB_STATUS_OK;
}

bb_status bb_raster_shift_rgb(
  bb_context *context,
  const bb_surface *source,
  bb_rgba8 source_base,
  bb_rgba8 target_base,
  bb_surface **out_surface
) {
  const uint8_t source_channels[3] = {source_base.red, source_base.green, source_base.blue};
  const uint8_t target_channels[3] = {target_base.red, target_base.green, target_base.blue};
  bb_surface *output;
  size_t offset;
  bb_status status = bb_surface_clone(context, source, &output);
  if (status != BB_STATUS_OK) {
    return status;
  }
  for (offset = 0; offset < output->pixel_bytes; offset += 4) {
    uint8_t channel;
    for (channel = 0; channel < 3; channel += 1) {
      output->pixels[offset + channel] = bb_raster_clamp_even_u8(
        (double)target_channels[channel] + source->pixels[offset + channel] - source_channels[channel]
      );
    }
  }
  *out_surface = output;
  return BB_STATUS_OK;
}
