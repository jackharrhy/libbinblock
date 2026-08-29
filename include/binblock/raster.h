#ifndef BINBLOCK_RASTER_H
#define BINBLOCK_RASTER_H

#include <binblock/binblock.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct bb_surface bb_surface;

typedef enum bb_pixel_format {
  BB_PIXEL_FORMAT_RGBA8_UNORM = 1
} bb_pixel_format;

typedef enum bb_color_space {
  BB_COLOR_SPACE_NUMERIC_SRGB = 1
} bb_color_space;

typedef enum bb_alpha_mode {
  BB_ALPHA_MODE_STRAIGHT = 1
} bb_alpha_mode;

typedef enum bb_mask_mode {
  BB_MASK_MULTIPLY = 0,
  BB_MASK_REPLACE = 1
} bb_mask_mode;

typedef enum bb_easing {
  BB_EASING_LINEAR = 0,
  BB_EASING_SMOOTHSTEP = 1,
  BB_EASING_REFERENCE = 2
} bb_easing;

typedef enum bb_alpha_metric {
  BB_ALPHA_METRIC_X = 0,
  BB_ALPHA_METRIC_Y = 1,
  BB_ALPHA_METRIC_EUCLIDEAN = 2,
  BB_ALPHA_METRIC_CHEBYSHEV = 3,
  BB_ALPHA_METRIC_BORDER = 4
} bb_alpha_metric;

typedef enum bb_alpha_direction {
  BB_ALPHA_DIRECTION_OUT = 0,
  BB_ALPHA_DIRECTION_IN = 1
} bb_alpha_direction;

typedef enum bb_gradient_preset {
  BB_GRADIENT_TOP_DOWN = 0,
  BB_GRADIENT_BOTTOM_UP = 1,
  BB_GRADIENT_LEFT_RIGHT = 2,
  BB_GRADIENT_RIGHT_LEFT = 3,
  BB_GRADIENT_RADIAL_IN = 4,
  BB_GRADIENT_RADIAL_OUT = 5,
  BB_GRADIENT_DIAGONAL = 6,
  BB_GRADIENT_CORNER = 7
} bb_gradient_preset;

typedef struct bb_rgba8 {
  uint8_t red;
  uint8_t green;
  uint8_t blue;
  uint8_t alpha;
} bb_rgba8;

typedef struct bb_image_desc {
  uint32_t width;
  uint32_t height;
  size_t row_pitch;
  bb_pixel_format format;
  bb_color_space color_space;
  bb_alpha_mode alpha_mode;
} bb_image_desc;

typedef struct bb_image_view {
  bb_image_desc desc;
  uint8_t *data;
  size_t data_length;
} bb_image_view;

typedef struct bb_const_image_view {
  bb_image_desc desc;
  const uint8_t *data;
  size_t data_length;
} bb_const_image_view;

typedef struct bb_gradient_stop {
  double offset;
  bb_rgba8 color;
  bb_easing easing;
  uint32_t has_easing;
} bb_gradient_stop;

typedef struct bb_alpha_field_desc {
  uint32_t width;
  uint32_t height;
  bb_alpha_metric metric;
  double center_x;
  double center_y;
  double radius;
  bb_alpha_direction direction;
  bb_easing easing;
  bb_rgba8 color;
  const uint8_t *levels;
  size_t level_count;
  uint32_t reference_radial_rounding;
} bb_alpha_field_desc;

typedef struct bb_linear_gradient_desc {
  uint32_t width;
  uint32_t height;
  double angle_degrees;
  double extent;
  uint32_t has_explicit_extent;
  const bb_gradient_stop *stops;
  size_t stop_count;
  bb_easing easing;
} bb_linear_gradient_desc;

typedef struct bb_elliptical_gradient_desc {
  uint32_t width;
  uint32_t height;
  double center_x;
  double center_y;
  double radius_x;
  double radius_y;
  double rotation_radians;
  const bb_gradient_stop *stops;
  size_t stop_count;
  bb_easing easing;
  uint32_t reference_radial_rounding;
} bb_elliptical_gradient_desc;

/* The context must outlive every surface allocated from it. */
BB_API bb_status bb_surface_create(bb_context *context, uint32_t width, uint32_t height, bb_surface **out_surface);
BB_API bb_status bb_surface_create_from_rgba8(
  bb_context *context,
  const bb_const_image_view *source,
  bb_surface **out_surface
);
BB_API void bb_surface_destroy(bb_surface *surface);
BB_API bb_status bb_surface_get_view(bb_surface *surface, bb_image_view *out_view);
BB_API bb_status bb_surface_get_const_view(const bb_surface *surface, bb_const_image_view *out_view);
BB_API bb_status bb_surface_clear(bb_surface *surface, bb_rgba8 color);

BB_API bb_status bb_raster_fill(
  bb_context *context,
  uint32_t width,
  uint32_t height,
  bb_rgba8 color,
  bb_surface **out_surface
);
BB_API bb_status bb_raster_crop(
  bb_context *context,
  const bb_surface *source,
  int32_t x,
  int32_t y,
  uint32_t width,
  uint32_t height,
  bb_surface **out_surface
);
BB_API bb_status bb_raster_canvas(
  bb_context *context,
  const bb_surface *source,
  uint32_t width,
  uint32_t height,
  int32_t x,
  int32_t y,
  bb_surface **out_surface
);
BB_API bb_status bb_raster_rotate_quarter_turns(
  bb_context *context,
  const bb_surface *source,
  int32_t turns,
  bb_surface **out_surface
);
BB_API bb_status bb_raster_opacity(
  bb_context *context,
  const bb_surface *source,
  double opacity,
  bb_surface **out_surface
);
BB_API bb_status bb_raster_invert_alpha(bb_context *context, const bb_surface *source, bb_surface **out_surface);
BB_API bb_status bb_raster_set_visible_rgb(
  bb_context *context,
  const bb_surface *source,
  bb_rgba8 color,
  bb_surface **out_surface
);
BB_API bb_status bb_raster_tint_chroma(
  bb_context *context,
  const bb_surface *source,
  bb_rgba8 color,
  bb_surface **out_surface
);
BB_API bb_status bb_raster_remap_two_color(
  bb_context *context,
  const bb_surface *source,
  bb_rgba8 source_foreground,
  bb_rgba8 source_background,
  bb_rgba8 foreground,
  bb_rgba8 background,
  bb_surface **out_surface
);
BB_API bb_status bb_raster_shift_rgb(
  bb_context *context,
  const bb_surface *source,
  bb_rgba8 source_base,
  bb_rgba8 target_base,
  bb_surface **out_surface
);
BB_API bb_status bb_raster_source_over(
  bb_context *context,
  const bb_surface *destination,
  const bb_surface *source,
  int32_t offset_x,
  int32_t offset_y,
  double opacity,
  bb_surface **out_surface
);
BB_API bb_status bb_raster_apply_mask(
  bb_context *context,
  const bb_surface *source,
  const bb_surface *mask,
  bb_mask_mode mode,
  bb_surface **out_surface
);
BB_API bb_status bb_raster_alpha_field(
  bb_context *context,
  const bb_alpha_field_desc *desc,
  bb_surface **out_surface
);
BB_API bb_status bb_raster_preset_gradient(
  bb_context *context,
  uint32_t width,
  uint32_t height,
  bb_gradient_preset preset,
  int32_t quarter_turns,
  bb_rgba8 color,
  bb_surface **out_surface
);
BB_API bb_status bb_raster_linear_gradient(
  bb_context *context,
  const bb_linear_gradient_desc *desc,
  bb_surface **out_surface
);
BB_API bb_status bb_raster_elliptical_gradient(
  bb_context *context,
  const bb_elliptical_gradient_desc *desc,
  bb_surface **out_surface
);
BB_API bb_status bb_raster_resize_lanczos3(
  bb_context *context,
  const bb_surface *source,
  uint32_t width,
  uint32_t height,
  bb_surface **out_surface
);

#ifdef __cplusplus
}
#endif

#endif
