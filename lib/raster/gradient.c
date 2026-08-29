#include <binblock/raster.h>

#include "checked_math.h"
#include "context_internal.h"
#include "raster_internal.h"

#include <math.h>
#include <string.h>

static int bb_easing_is_valid(bb_easing easing) {
  return easing == BB_EASING_LINEAR || easing == BB_EASING_SMOOTHSTEP || easing == BB_EASING_LEGACY;
}

static double bb_clamp_unit(double value) {
  if (value < 0.0) return 0.0;
  if (value > 1.0) return 1.0;
  return value;
}

static double bb_apply_easing(double value, bb_easing easing) {
  const double t = bb_clamp_unit(value);
  if (easing == BB_EASING_SMOOTHSTEP) {
    return t * t * (3.0 - 2.0 * t);
  }
  if (easing == BB_EASING_LEGACY) {
    return 0.5 * t + 0.5 * (3.0 * t * t - 2.0 * t * t * t);
  }
  return t;
}

static int bb_legacy_radial_distance(double distance_squared) {
  static const uint16_t distances[] = {98, 116, 234, 433, 601, 720, 922};
  size_t index;
  for (index = 0; index < sizeof(distances) / sizeof(distances[0]); index += 1) {
    if (distance_squared == distances[index]) return 1;
  }
  return 0;
}

static int bb_alpha_metric_is_valid(bb_alpha_metric metric) {
  return metric >= BB_ALPHA_METRIC_X && metric <= BB_ALPHA_METRIC_BORDER;
}

bb_status bb_raster_alpha_field(
  bb_context *context,
  const bb_alpha_field_desc *desc,
  bb_surface **out_surface
) {
  bb_surface *output;
  uint32_t y;
  bb_status status;
  if (out_surface == NULL) {
    return BB_STATUS_INVALID_ARGUMENT;
  }
  *out_surface = NULL;
  if (context == NULL || desc == NULL || desc->width == 0 || desc->height == 0 ||
      !bb_alpha_metric_is_valid(desc->metric) || !bb_easing_is_valid(desc->easing) || !isfinite(desc->radius) ||
      desc->radius == 0.0 || (desc->direction != BB_ALPHA_DIRECTION_OUT && desc->direction != BB_ALPHA_DIRECTION_IN) ||
      (desc->level_count != 0 && desc->levels == NULL)) {
    return BB_STATUS_INVALID_ARGUMENT;
  }
  status = bb_surface_allocate(context, desc->width, desc->height, &output);
  if (status != BB_STATUS_OK) {
    return status;
  }
  for (y = 0; y < desc->height; y += 1) {
    uint32_t x;
    for (x = 0; x < desc->width; x += 1) {
      const double delta_x = (double)x - desc->center_x;
      const double delta_y = (double)y - desc->center_y;
      const double distance_squared = delta_x * delta_x + delta_y * delta_y;
      double distance;
      int32_t alpha;
      uint8_t *pixel = output->pixels + (size_t)y * output->desc.row_pitch + (size_t)x * 4;
      if (desc->metric == BB_ALPHA_METRIC_X) distance = delta_x;
      else if (desc->metric == BB_ALPHA_METRIC_Y) distance = delta_y;
      else if (desc->metric == BB_ALPHA_METRIC_EUCLIDEAN) distance = sqrt(distance_squared);
      else if (desc->metric == BB_ALPHA_METRIC_CHEBYSHEV) distance = fmax(fabs(delta_x), fabs(delta_y));
      else distance = fmin(fmin(x, y), fmin(desc->width - 1 - x, desc->height - 1 - y));

      if (desc->metric == BB_ALPHA_METRIC_BORDER && desc->level_count != 0) {
        size_t level_index = distance <= 0.0 ? 0 : (size_t)floor(distance);
        if (level_index >= desc->level_count) level_index = desc->level_count - 1;
        alpha = desc->levels[level_index];
      } else {
        const double amount = bb_apply_easing(distance / desc->radius, desc->easing);
        alpha = bb_raster_round_u8(255.0 * (desc->direction == BB_ALPHA_DIRECTION_IN ? 1.0 - amount : amount));
        if (desc->legacy_radial_rounding && desc->metric == BB_ALPHA_METRIC_EUCLIDEAN &&
            bb_legacy_radial_distance(distance_squared)) {
          alpha += desc->direction == BB_ALPHA_DIRECTION_IN ? 1 : -1;
        }
      }
      pixel[0] = desc->color.red;
      pixel[1] = desc->color.green;
      pixel[2] = desc->color.blue;
      pixel[3] = bb_raster_round_u8(((double)alpha * desc->color.alpha) / 255.0);
    }
  }
  *out_surface = output;
  return BB_STATUS_OK;
}

bb_status bb_raster_preset_gradient(
  bb_context *context,
  uint32_t width,
  uint32_t height,
  bb_gradient_preset preset,
  int32_t quarter_turns,
  bb_rgba8 color,
  bb_surface **out_surface
) {
  bb_surface *output;
  const int32_t turns = ((quarter_turns % 4) + 4) % 4;
  uint32_t y;
  bb_status status;
  if (out_surface == NULL) {
    return BB_STATUS_INVALID_ARGUMENT;
  }
  *out_surface = NULL;
  if (context == NULL || width == 0 || height == 0 || preset < BB_GRADIENT_TOP_DOWN || preset > BB_GRADIENT_CORNER) {
    return BB_STATUS_INVALID_ARGUMENT;
  }
  status = bb_surface_allocate(context, width, height, &output);
  if (status != BB_STATUS_OK) {
    return status;
  }
  for (y = 0; y < height; y += 1) {
    uint32_t x;
    for (x = 0; x < width; x += 1) {
      double normalized_x = width == 1 ? 0.5 : (double)x / (width - 1);
      double normalized_y = height == 1 ? 0.5 : (double)y / (height - 1);
      double rotated_x;
      double rotated_y;
      double amount;
      uint8_t *pixel = output->pixels + (size_t)y * output->desc.row_pitch + (size_t)x * 4;
      if (turns == 1) {
        rotated_x = 1.0 - normalized_y;
        rotated_y = normalized_x;
      } else if (turns == 2) {
        rotated_x = 1.0 - normalized_x;
        rotated_y = 1.0 - normalized_y;
      } else if (turns == 3) {
        rotated_x = normalized_y;
        rotated_y = 1.0 - normalized_x;
      } else {
        rotated_x = normalized_x;
        rotated_y = normalized_y;
      }
      if (preset == BB_GRADIENT_TOP_DOWN) amount = 1.0 - rotated_y;
      else if (preset == BB_GRADIENT_BOTTOM_UP) amount = rotated_y;
      else if (preset == BB_GRADIENT_LEFT_RIGHT) amount = 1.0 - rotated_x;
      else if (preset == BB_GRADIENT_RIGHT_LEFT) amount = rotated_x;
      else if (preset == BB_GRADIENT_RADIAL_IN) {
        amount = fmax(0.0, 1.0 - hypot(rotated_x - 0.5, rotated_y - 0.5) / sqrt(0.5));
      } else if (preset == BB_GRADIENT_RADIAL_OUT) {
        amount = fmin(1.0, hypot(rotated_x - 0.5, rotated_y - 0.5) / sqrt(0.5));
      } else if (preset == BB_GRADIENT_DIAGONAL) amount = 1.0 - (rotated_x + rotated_y) / 2.0;
      else amount = 1.0 - fmax(rotated_x, rotated_y);
      pixel[0] = color.red;
      pixel[1] = color.green;
      pixel[2] = color.blue;
      pixel[3] = bb_raster_round_u8(bb_clamp_unit(amount) * 255.0);
    }
  }
  *out_surface = output;
  return BB_STATUS_OK;
}

static bb_status bb_gradient_prepare_stops(
  bb_context *context,
  const bb_gradient_stop *stops,
  size_t stop_count,
  bb_gradient_stop **out_stops,
  size_t *out_bytes
) {
  bb_gradient_stop *copy;
  size_t bytes;
  size_t index;
  bb_status status;
  if (stops == NULL || stop_count < 2 || out_stops == NULL || out_bytes == NULL) {
    return BB_STATUS_INVALID_ARGUMENT;
  }
  *out_stops = NULL;
  *out_bytes = 0;
  if (!bb_size_multiply(stop_count, sizeof(*stops), &bytes)) {
    return BB_STATUS_OVERFLOW;
  }
  status = bb_context_allocate(context, bytes, _Alignof(bb_gradient_stop), (void **)&copy);
  if (status != BB_STATUS_OK) {
    return status;
  }
  memcpy(copy, stops, bytes);
  for (index = 0; index < stop_count; index += 1) {
    size_t cursor = index;
    bb_gradient_stop value = copy[index];
    if (!isfinite(value.offset) || value.offset < 0.0 || value.offset > 1.0 ||
        (value.has_easing && !bb_easing_is_valid(value.easing))) {
      bb_context_deallocate(context, copy, bytes, _Alignof(bb_gradient_stop));
      return BB_STATUS_INVALID_ARGUMENT;
    }
    while (cursor > 0 && copy[cursor - 1].offset > value.offset) {
      copy[cursor] = copy[cursor - 1];
      cursor -= 1;
    }
    copy[cursor] = value;
  }
  *out_stops = copy;
  *out_bytes = bytes;
  return BB_STATUS_OK;
}

static void bb_gradient_sample(
  const bb_gradient_stop *stops,
  size_t stop_count,
  double position,
  bb_easing default_easing,
  uint8_t output[4]
) {
  size_t right_index = 0;
  size_t left_index;
  double raw_amount;
  double amount;
  bb_easing easing;
  uint8_t channel;
  while (right_index < stop_count && stops[right_index].offset < position) {
    right_index += 1;
  }
  if (right_index == stop_count) right_index = stop_count - 1;
  left_index = right_index == 0 ? 0 : right_index - 1;
  if (stops[right_index].offset == stops[left_index].offset) raw_amount = 0.0;
  else raw_amount = (position - stops[left_index].offset) / (stops[right_index].offset - stops[left_index].offset);
  easing = stops[right_index].has_easing
             ? stops[right_index].easing
             : stops[left_index].has_easing ? stops[left_index].easing : default_easing;
  amount = bb_apply_easing(raw_amount, easing);
  for (channel = 0; channel < 4; channel += 1) {
    const uint8_t *left = (const uint8_t *)&stops[left_index].color;
    const uint8_t *right = (const uint8_t *)&stops[right_index].color;
    output[channel] = bb_raster_round_u8(left[channel] * (1.0 - amount) + right[channel] * amount);
  }
}

bb_status bb_raster_linear_gradient(
  bb_context *context,
  const bb_linear_gradient_desc *desc,
  bb_surface **out_surface
) {
  bb_gradient_stop *stops;
  size_t stop_bytes;
  bb_surface *output;
  double radians;
  double direction_x;
  double direction_y;
  double extent;
  double length;
  uint32_t y;
  bb_status status;
  if (out_surface == NULL) return BB_STATUS_INVALID_ARGUMENT;
  *out_surface = NULL;
  if (context == NULL || desc == NULL || desc->width == 0 || desc->height == 0 || !isfinite(desc->angle_degrees) ||
      !bb_easing_is_valid(desc->easing) || (desc->has_explicit_extent && (!isfinite(desc->extent) || desc->extent <= 0.0))) {
    return BB_STATUS_INVALID_ARGUMENT;
  }
  status = bb_gradient_prepare_stops(context, desc->stops, desc->stop_count, &stops, &stop_bytes);
  if (status != BB_STATUS_OK) return status;
  radians = desc->angle_degrees * (acos(-1.0) / 180.0);
  direction_x = sin(radians);
  direction_y = -cos(radians);
  if (fabs(direction_x) < 1e-12) direction_x = 0.0;
  else if (fabs(fabs(direction_x) - 1.0) < 1e-12) direction_x = copysign(1.0, direction_x);
  if (fabs(direction_y) < 1e-12) direction_y = 0.0;
  else if (fabs(fabs(direction_y) - 1.0) < 1e-12) direction_y = copysign(1.0, direction_y);
  extent = (fabs(direction_x) * fmax(1.0, desc->width - 1.0) +
            fabs(direction_y) * fmax(1.0, desc->height - 1.0)) /
           2.0;
  if (extent == 0.0) extent = 1.0;
  length = desc->has_explicit_extent ? desc->extent : 2.0 * extent;
  status = bb_surface_allocate(context, desc->width, desc->height, &output);
  if (status != BB_STATUS_OK) {
    bb_context_deallocate(context, stops, stop_bytes, _Alignof(bb_gradient_stop));
    return status;
  }
  for (y = 0; y < desc->height; y += 1) {
    uint32_t x;
    for (x = 0; x < desc->width; x += 1) {
      const double centered_x = desc->width == 1 ? 0.0 : x - (desc->width - 1.0) / 2.0;
      const double centered_y = desc->height == 1 ? 0.0 : y - (desc->height - 1.0) / 2.0;
      const double position = bb_clamp_unit((centered_x * direction_x + centered_y * direction_y + extent) / length);
      uint8_t *pixel = output->pixels + (size_t)y * output->desc.row_pitch + (size_t)x * 4;
      bb_gradient_sample(stops, desc->stop_count, position, desc->easing, pixel);
    }
  }
  bb_context_deallocate(context, stops, stop_bytes, _Alignof(bb_gradient_stop));
  *out_surface = output;
  return BB_STATUS_OK;
}

bb_status bb_raster_elliptical_gradient(
  bb_context *context,
  const bb_elliptical_gradient_desc *desc,
  bb_surface **out_surface
) {
  bb_gradient_stop *stops;
  size_t stop_bytes;
  bb_surface *output;
  double cosine;
  double sine;
  uint32_t y;
  bb_status status;
  if (out_surface == NULL) return BB_STATUS_INVALID_ARGUMENT;
  *out_surface = NULL;
  if (context == NULL || desc == NULL || desc->width == 0 || desc->height == 0 || !(desc->radius_x > 0.0) ||
      !(desc->radius_y > 0.0) || !isfinite(desc->radius_x) || !isfinite(desc->radius_y) ||
      !isfinite(desc->rotation_radians) || !bb_easing_is_valid(desc->easing)) {
    return BB_STATUS_INVALID_ARGUMENT;
  }
  status = bb_gradient_prepare_stops(context, desc->stops, desc->stop_count, &stops, &stop_bytes);
  if (status != BB_STATUS_OK) return status;
  cosine = cos(desc->rotation_radians);
  sine = sin(desc->rotation_radians);
  status = bb_surface_allocate(context, desc->width, desc->height, &output);
  if (status != BB_STATUS_OK) {
    bb_context_deallocate(context, stops, stop_bytes, _Alignof(bb_gradient_stop));
    return status;
  }
  for (y = 0; y < desc->height; y += 1) {
    uint32_t x;
    for (x = 0; x < desc->width; x += 1) {
      const double delta_x = x - desc->center_x;
      const double delta_y = y - desc->center_y;
      const double distance_squared = delta_x * delta_x + delta_y * delta_y;
      const double rotated_x = delta_x * cosine + delta_y * sine;
      const double rotated_y = -delta_x * sine + delta_y * cosine;
      const double distance = bb_clamp_unit(hypot(rotated_x / desc->radius_x, rotated_y / desc->radius_y));
      uint8_t *pixel = output->pixels + (size_t)y * output->desc.row_pitch + (size_t)x * 4;
      bb_gradient_sample(stops, desc->stop_count, distance, desc->easing, pixel);
      if (desc->legacy_radial_rounding && bb_legacy_radial_distance(distance_squared)) {
        const int32_t alpha = pixel[3] + (stops[0].color.alpha > stops[desc->stop_count - 1].color.alpha ? 1 : -1);
        pixel[3] = bb_raster_round_u8(alpha);
      }
    }
  }
  bb_context_deallocate(context, stops, stop_bytes, _Alignof(bb_gradient_stop));
  *out_surface = output;
  return BB_STATUS_OK;
}
