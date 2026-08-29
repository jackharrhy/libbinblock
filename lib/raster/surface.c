#include <binblock/raster.h>

#include "checked_math.h"
#include "context_internal.h"
#include "raster_internal.h"

#include <string.h>

static bb_status bb_image_desc_validate(const bb_const_image_view *view, size_t *out_required_bytes) {
  size_t row_bytes;
  size_t preceding_rows;
  size_t required_bytes;
  if (view == NULL || out_required_bytes == NULL || view->desc.width == 0 || view->desc.height == 0 ||
      view->desc.format != BB_PIXEL_FORMAT_RGBA8_UNORM || view->desc.color_space != BB_COLOR_SPACE_NUMERIC_SRGB ||
      view->desc.alpha_mode != BB_ALPHA_MODE_STRAIGHT) {
    return BB_STATUS_INVALID_ARGUMENT;
  }
  if (!bb_size_multiply(view->desc.width, 4, &row_bytes)) {
    return BB_STATUS_OVERFLOW;
  }
  if (view->desc.row_pitch < row_bytes) {
    return BB_STATUS_INVALID_ARGUMENT;
  }
  if (!bb_size_multiply(view->desc.height - 1, view->desc.row_pitch, &preceding_rows) ||
      !bb_size_add(preceding_rows, row_bytes, &required_bytes)) {
    return BB_STATUS_OVERFLOW;
  }
  if (required_bytes > view->data_length || (required_bytes != 0 && view->data == NULL)) {
    return BB_STATUS_INVALID_ARGUMENT;
  }
  *out_required_bytes = required_bytes;
  return BB_STATUS_OK;
}

bb_status bb_surface_allocate(bb_context *context, uint32_t width, uint32_t height, bb_surface **out_surface) {
  bb_surface *surface = NULL;
  bb_limits limits;
  uint64_t pixel_count;
  size_t row_pitch;
  size_t pixel_bytes;
  bb_status status;
  if (out_surface == NULL) {
    return BB_STATUS_INVALID_ARGUMENT;
  }
  *out_surface = NULL;
  if (context == NULL || width == 0 || height == 0) {
    return BB_STATUS_INVALID_ARGUMENT;
  }
  pixel_count = (uint64_t)width * (uint64_t)height;
  status = bb_context_get_limits(context, &limits);
  if (status != BB_STATUS_OK) return status;
  if (pixel_count > limits.max_render_pixels) return BB_STATUS_LIMIT_EXCEEDED;
  if (!bb_size_multiply(width, 4, &row_pitch) || !bb_size_multiply(row_pitch, height, &pixel_bytes)) {
    return BB_STATUS_OVERFLOW;
  }
  status = bb_context_allocate(context, sizeof(*surface), _Alignof(bb_surface), (void **)&surface);
  if (status != BB_STATUS_OK) {
    return status;
  }
  memset(surface, 0, sizeof(*surface));
  status = bb_context_allocate(context, pixel_bytes, _Alignof(uint8_t), (void **)&surface->pixels);
  if (status != BB_STATUS_OK) {
    bb_context_deallocate(context, surface, sizeof(*surface), _Alignof(bb_surface));
    return status;
  }
  memset(surface->pixels, 0, pixel_bytes);
  surface->context = context;
  surface->desc.width = width;
  surface->desc.height = height;
  surface->desc.row_pitch = row_pitch;
  surface->desc.format = BB_PIXEL_FORMAT_RGBA8_UNORM;
  surface->desc.color_space = BB_COLOR_SPACE_NUMERIC_SRGB;
  surface->desc.alpha_mode = BB_ALPHA_MODE_STRAIGHT;
  surface->pixel_bytes = pixel_bytes;
  *out_surface = surface;
  return BB_STATUS_OK;
}

bb_status bb_surface_create(bb_context *context, uint32_t width, uint32_t height, bb_surface **out_surface) {
  return bb_surface_allocate(context, width, height, out_surface);
}

bb_status bb_surface_create_from_rgba8(
  bb_context *context,
  const bb_const_image_view *source,
  bb_surface **out_surface
) {
  bb_surface *surface;
  size_t required_bytes;
  size_t row_bytes;
  uint32_t y;
  bb_status status;
  if (out_surface == NULL) {
    return BB_STATUS_INVALID_ARGUMENT;
  }
  *out_surface = NULL;
  if (context == NULL) {
    return BB_STATUS_INVALID_ARGUMENT;
  }
  status = bb_image_desc_validate(source, &required_bytes);
  if (status != BB_STATUS_OK) {
    return status;
  }
  (void)required_bytes;
  status = bb_surface_allocate(context, source->desc.width, source->desc.height, &surface);
  if (status != BB_STATUS_OK) {
    return status;
  }
  row_bytes = (size_t)source->desc.width * 4;
  for (y = 0; y < source->desc.height; y += 1) {
    memcpy(surface->pixels + (size_t)y * surface->desc.row_pitch, source->data + (size_t)y * source->desc.row_pitch, row_bytes);
  }
  *out_surface = surface;
  return BB_STATUS_OK;
}

void bb_surface_destroy(bb_surface *surface) {
  bb_context *context;
  if (surface == NULL) {
    return;
  }
  context = surface->context;
  bb_context_deallocate(context, surface->pixels, surface->pixel_bytes, _Alignof(uint8_t));
  bb_context_deallocate(context, surface, sizeof(*surface), _Alignof(bb_surface));
}

bb_status bb_surface_get_view(bb_surface *surface, bb_image_view *out_view) {
  if (surface == NULL || out_view == NULL) {
    return BB_STATUS_INVALID_ARGUMENT;
  }
  out_view->desc = surface->desc;
  out_view->data = surface->pixels;
  out_view->data_length = surface->pixel_bytes;
  return BB_STATUS_OK;
}

bb_status bb_surface_get_const_view(const bb_surface *surface, bb_const_image_view *out_view) {
  if (surface == NULL || out_view == NULL) {
    return BB_STATUS_INVALID_ARGUMENT;
  }
  out_view->desc = surface->desc;
  out_view->data = surface->pixels;
  out_view->data_length = surface->pixel_bytes;
  return BB_STATUS_OK;
}

bb_status bb_surface_clear(bb_surface *surface, bb_rgba8 color) {
  uint32_t y;
  uint32_t x;
  if (surface == NULL) {
    return BB_STATUS_INVALID_ARGUMENT;
  }
  for (y = 0; y < surface->desc.height; y += 1) {
    uint8_t *row = surface->pixels + (size_t)y * surface->desc.row_pitch;
    for (x = 0; x < surface->desc.width; x += 1) {
      const size_t offset = (size_t)x * 4;
      row[offset] = color.red;
      row[offset + 1] = color.green;
      row[offset + 2] = color.blue;
      row[offset + 3] = color.alpha;
    }
  }
  return BB_STATUS_OK;
}

bb_status bb_surface_clone(bb_context *context, const bb_surface *source, bb_surface **out_surface) {
  bb_surface *clone;
  bb_status status;
  if (out_surface == NULL) {
    return BB_STATUS_INVALID_ARGUMENT;
  }
  *out_surface = NULL;
  if (context == NULL || source == NULL) {
    return BB_STATUS_INVALID_ARGUMENT;
  }
  status = bb_surface_allocate(context, source->desc.width, source->desc.height, &clone);
  if (status != BB_STATUS_OK) {
    return status;
  }
  memcpy(clone->pixels, source->pixels, source->pixel_bytes);
  *out_surface = clone;
  return BB_STATUS_OK;
}

bb_status bb_raster_fill(
  bb_context *context,
  uint32_t width,
  uint32_t height,
  bb_rgba8 color,
  bb_surface **out_surface
) {
  bb_surface *surface;
  bb_status status;
  if (out_surface == NULL) {
    return BB_STATUS_INVALID_ARGUMENT;
  }
  *out_surface = NULL;
  status = bb_surface_allocate(context, width, height, &surface);
  if (status != BB_STATUS_OK) {
    return status;
  }
  status = bb_surface_clear(surface, color);
  if (status != BB_STATUS_OK) {
    bb_surface_destroy(surface);
    return status;
  }
  *out_surface = surface;
  return BB_STATUS_OK;
}

uint8_t bb_raster_round_u8(double value) {
  if (!(value > 0.0)) {
    return 0;
  }
  if (value >= 255.0) {
    return UINT8_MAX;
  }
  return (uint8_t)(value + 0.5);
}

uint8_t bb_raster_clamp_even_u8(double value) {
  uint32_t lower;
  double fraction;
  if (!(value > 0.0)) {
    return 0;
  }
  if (value >= 255.0) {
    return UINT8_MAX;
  }
  lower = (uint32_t)value;
  fraction = value - (double)lower;
  if (fraction > 0.5 || (fraction == 0.5 && (lower & UINT32_C(1)) != 0)) {
    lower += 1;
  }
  return (uint8_t)lower;
}

int bb_raster_unit_value_is_valid(double value) {
  return value == value && value >= 0.0 && value <= 1.0;
}
