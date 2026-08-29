#include <binblock/raster.h>

#include "checked_math.h"
#include "context_internal.h"
#include "raster_internal.h"

#include <math.h>
#include <stdint.h>

typedef struct bb_lanczos_axis {
  uint32_t source_size;
  uint32_t destination_size;
  double scale;
  double filter_scale;
  double support;
} bb_lanczos_axis;

static double bb_sinc(double value) {
  double angle;
  if (value == 0.0) return 1.0;
  angle = acos(-1.0) * value;
  return sin(angle) / angle;
}

static double bb_lanczos(double value) {
  if (fabs(value) >= 3.0) return 0.0;
  return bb_sinc(value) * bb_sinc(value / 3.0);
}

static bb_lanczos_axis bb_lanczos_axis_create(uint32_t source_size, uint32_t destination_size) {
  bb_lanczos_axis axis;
  axis.source_size = source_size;
  axis.destination_size = destination_size;
  axis.scale = (double)source_size / destination_size;
  axis.filter_scale = fmax(1.0, axis.scale);
  axis.support = 3.0 * axis.filter_scale;
  return axis;
}

static void bb_lanczos_range(
  const bb_lanczos_axis *axis,
  uint32_t destination,
  double *out_center,
  int64_t *out_start,
  int64_t *out_end
) {
  const double center = (destination + 0.5) * axis->scale - 0.5;
  *out_center = center;
  *out_start = (int64_t)ceil(center - axis->support);
  *out_end = (int64_t)floor(center + axis->support);
}

static double bb_lanczos_weight_total(
  const bb_lanczos_axis *axis,
  double center,
  int64_t start,
  int64_t end
) {
  double total = 0.0;
  int64_t source;
  for (source = start; source <= end; source += 1) {
    const double weight = bb_lanczos((center - source) / axis->filter_scale);
    if (weight == 0.0 || source < 0 || source >= axis->source_size) continue;
    total += weight;
  }
  return total;
}

bb_status bb_raster_resize_lanczos3(
  bb_context *context,
  const bb_surface *source,
  uint32_t width,
  uint32_t height,
  bb_surface **out_surface
) {
  bb_lanczos_axis horizontal_axis;
  bb_lanczos_axis vertical_axis;
  double *horizontal = NULL;
  size_t horizontal_pixels;
  size_t horizontal_values;
  size_t horizontal_bytes;
  bb_surface *output;
  uint32_t y;
  bb_status status;
  if (out_surface == NULL) return BB_STATUS_INVALID_ARGUMENT;
  *out_surface = NULL;
  if (context == NULL || source == NULL || width == 0 || height == 0) return BB_STATUS_INVALID_ARGUMENT;
  if (!bb_size_multiply(width, source->desc.height, &horizontal_pixels) ||
      !bb_size_multiply(horizontal_pixels, 4, &horizontal_values) ||
      !bb_size_multiply(horizontal_values, sizeof(*horizontal), &horizontal_bytes)) {
    return BB_STATUS_OVERFLOW;
  }
  status = bb_context_allocate(context, horizontal_bytes, _Alignof(double), (void **)&horizontal);
  if (status != BB_STATUS_OK) return status;
  for (size_t index = 0; index < horizontal_values; index += 1) horizontal[index] = 0.0;
  horizontal_axis = bb_lanczos_axis_create(source->desc.width, width);
  vertical_axis = bb_lanczos_axis_create(source->desc.height, height);

  for (y = 0; y < source->desc.height; y += 1) {
    uint32_t x;
    for (x = 0; x < width; x += 1) {
      double center;
      int64_t start;
      int64_t end;
      double total;
      int64_t source_x;
      double *intermediate = horizontal + ((size_t)y * width + x) * 4;
      bb_lanczos_range(&horizontal_axis, x, &center, &start, &end);
      total = bb_lanczos_weight_total(&horizontal_axis, center, start, end);
      if (total == 0.0) {
        bb_context_deallocate(context, horizontal, horizontal_bytes, _Alignof(double));
        return BB_STATUS_INTERNAL_ERROR;
      }
      for (source_x = start; source_x <= end; source_x += 1) {
        double weight = bb_lanczos((center - source_x) / horizontal_axis.filter_scale);
        const uint8_t *pixel;
        double alpha;
        if (weight == 0.0 || source_x < 0 || source_x >= source->desc.width) continue;
        weight /= total;
        pixel = source->pixels + (size_t)y * source->desc.row_pitch + (size_t)source_x * 4;
        alpha = pixel[3] / 255.0;
        intermediate[0] += pixel[0] * alpha * weight;
        intermediate[1] += pixel[1] * alpha * weight;
        intermediate[2] += pixel[2] * alpha * weight;
        intermediate[3] += pixel[3] * weight;
      }
    }
  }

  status = bb_surface_allocate(context, width, height, &output);
  if (status != BB_STATUS_OK) {
    bb_context_deallocate(context, horizontal, horizontal_bytes, _Alignof(double));
    return status;
  }
  for (y = 0; y < height; y += 1) {
    double center;
    int64_t start;
    int64_t end;
    double total;
    uint32_t x;
    bb_lanczos_range(&vertical_axis, y, &center, &start, &end);
    total = bb_lanczos_weight_total(&vertical_axis, center, start, end);
    if (total == 0.0) {
      bb_surface_destroy(output);
      bb_context_deallocate(context, horizontal, horizontal_bytes, _Alignof(double));
      return BB_STATUS_INTERNAL_ERROR;
    }
    for (x = 0; x < width; x += 1) {
      double accumulated[4] = {0.0, 0.0, 0.0, 0.0};
      int64_t source_y;
      uint8_t *pixel = output->pixels + (size_t)y * output->desc.row_pitch + (size_t)x * 4;
      for (source_y = start; source_y <= end; source_y += 1) {
        double weight = bb_lanczos((center - source_y) / vertical_axis.filter_scale);
        const double *intermediate;
        uint8_t channel;
        if (weight == 0.0 || source_y < 0 || source_y >= source->desc.height) continue;
        weight /= total;
        intermediate = horizontal + ((size_t)source_y * width + x) * 4;
        for (channel = 0; channel < 4; channel += 1) accumulated[channel] += intermediate[channel] * weight;
      }
      if (accumulated[3] < 0.0) accumulated[3] = 0.0;
      if (accumulated[3] > 255.0) accumulated[3] = 255.0;
      pixel[3] = bb_raster_round_u8(accumulated[3]);
      if (accumulated[3] > 0.0) {
        pixel[0] = bb_raster_round_u8((accumulated[0] * 255.0) / accumulated[3]);
        pixel[1] = bb_raster_round_u8((accumulated[1] * 255.0) / accumulated[3]);
        pixel[2] = bb_raster_round_u8((accumulated[2] * 255.0) / accumulated[3]);
      }
    }
  }
  bb_context_deallocate(context, horizontal, horizontal_bytes, _Alignof(double));
  *out_surface = output;
  return BB_STATUS_OK;
}
