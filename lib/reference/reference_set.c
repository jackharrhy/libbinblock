#include <binblock/reference_set.h>

#include "raster_internal.h"

#include <string.h>

typedef struct bb_reference_alpha_map_spec {
  uint32_t width;
  uint32_t height;
  int32_t offset_x;
  int32_t offset_y;
  uint32_t geometry;
} bb_reference_alpha_map_spec;

static const bb_reference_alpha_map_spec BB_REFERENCE_ALPHA_MAP_SPECS[18] = {
  {64, 64, 0, 0, 0},
  {64, 64, 0, 0, 1},
  {64, 64, 0, 0, 2},
  {63, 64, 1, 0, 3},
  {63, 63, 1, 1, 4},
  {64, 64, 0, 0, 5},
  {63, 63, 1, 1, 6},
  {64, 64, 0, 0, 7},
  {62, 62, 1, 1, 8},
  {64, 64, 0, 0, 0},
  {64, 63, 0, 1, 1},
  {64, 64, 0, 0, 2},
  {63, 64, 1, 0, 3},
  {63, 63, 1, 1, 4},
  {64, 64, 0, 0, 5},
  {64, 64, 0, 0, 7},
  {63, 63, 1, 1, 6},
  {62, 62, 1, 1, 8},
};

static bb_status bb_reference_render_analytic_alpha_map(
  bb_context *context,
  uint32_t index,
  const bb_reference_alpha_map_spec *spec,
  bb_surface **out_surface
) {
  static const uint8_t border_levels[] = {53, 101, 146, 179, 206, 255};
  bb_alpha_field_desc desc;
  memset(&desc, 0, sizeof(desc));
  desc.width = spec->width;
  desc.height = spec->height;
  desc.direction = BB_ALPHA_DIRECTION_IN;
  desc.easing = BB_EASING_REFERENCE;
  desc.color = index < 9 ? (bb_rgba8){0, 0, 0, 255} : (bb_rgba8){255, 255, 255, 255};
  if (spec->geometry == 0) {
    desc.metric = BB_ALPHA_METRIC_Y;
    desc.radius = 64;
  } else if (spec->geometry == 1) {
    desc.metric = BB_ALPHA_METRIC_Y;
    desc.center_y = 63;
    desc.radius = -64;
  } else if (spec->geometry == 2) {
    desc.metric = BB_ALPHA_METRIC_X;
    desc.radius = 64;
  } else if (spec->geometry == 3) {
    desc.metric = BB_ALPHA_METRIC_X;
    desc.center_x = 63;
    desc.radius = -64;
  } else if (spec->geometry == 4) {
    desc.metric = BB_ALPHA_METRIC_EUCLIDEAN;
    desc.center_x = 31;
    desc.center_y = 31;
    desc.radius = 32;
    desc.reference_radial_rounding = 1;
  } else if (spec->geometry == 5) {
    desc.metric = BB_ALPHA_METRIC_EUCLIDEAN;
    desc.center_x = 32;
    desc.center_y = 32;
    desc.radius = 32;
    desc.direction = BB_ALPHA_DIRECTION_OUT;
    desc.reference_radial_rounding = 1;
  } else if (spec->geometry == 6) {
    desc.metric = BB_ALPHA_METRIC_CHEBYSHEV;
    desc.center_x = 31;
    desc.center_y = 31;
    desc.radius = 32;
  } else if (spec->geometry == 7) {
    desc.metric = BB_ALPHA_METRIC_CHEBYSHEV;
    desc.center_x = 32;
    desc.center_y = 32;
    desc.radius = 32;
    desc.direction = BB_ALPHA_DIRECTION_OUT;
  } else {
    desc.metric = BB_ALPHA_METRIC_BORDER;
    desc.radius = 1;
    desc.levels = border_levels;
    desc.level_count = sizeof(border_levels);
  }
  return bb_raster_alpha_field(context, &desc, out_surface);
}

bb_status bb_reference_set_alpha_map(
  bb_context *context,
  uint32_t index,
  const bb_surface *map_18_raster,
  bb_reference_alpha_map *out_map
) {
  bb_const_image_view view;
  bb_status status;
  if (out_map == NULL) return BB_STATUS_INVALID_ARGUMENT;
  memset(out_map, 0, sizeof(*out_map));
  if (context == NULL || index > 18) return BB_STATUS_INVALID_ARGUMENT;
  if (index == 18) {
    if (map_18_raster == NULL) return BB_STATUS_NOT_FOUND;
    status = bb_surface_get_const_view(map_18_raster, &view);
    if (status != BB_STATUS_OK || view.desc.width != 64 || view.desc.height != 64) return BB_STATUS_INVALID_ARGUMENT;
    status = bb_surface_clone(context, map_18_raster, &out_map->surface);
    if (status != BB_STATUS_OK) return status;
    out_map->raster_fallback = 1;
    return BB_STATUS_OK;
  }
  status = bb_reference_render_analytic_alpha_map(context, index, &BB_REFERENCE_ALPHA_MAP_SPECS[index], &out_map->surface);
  if (status != BB_STATUS_OK) return status;
  out_map->offset_x = BB_REFERENCE_ALPHA_MAP_SPECS[index].offset_x;
  out_map->offset_y = BB_REFERENCE_ALPHA_MAP_SPECS[index].offset_y;
  return BB_STATUS_OK;
}
