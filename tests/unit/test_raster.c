#include "test_support.h"

#include <binblock/raster.h>
#include <binblock/reference_set.h>

#include "context_internal.h"
#include "png_fixture.h"

#include <math.h>
#include <stdint.h>
#include <string.h>

static bb_status surface_from_bytes(
  bb_context *context,
  uint32_t width,
  uint32_t height,
  const uint8_t *pixels,
  bb_surface **out_surface
) {
  const bb_const_image_view view = {
    {width, height, (size_t)width * 4, BB_PIXEL_FORMAT_RGBA8_UNORM, BB_COLOR_SPACE_NUMERIC_SRGB, BB_ALPHA_MODE_STRAIGHT},
    pixels,
    (size_t)width * height * 4,
  };
  return bb_surface_create_from_rgba8(context, &view, out_surface);
}

static int surface_equals(const bb_surface *surface, uint32_t width, uint32_t height, const uint8_t *expected) {
  bb_const_image_view view;
  if (bb_surface_get_const_view(surface, &view) != BB_STATUS_OK) return 0;
  return view.desc.width == width && view.desc.height == height && view.desc.row_pitch == (size_t)width * 4 &&
         view.data_length == (size_t)width * height * 4 && memcmp(view.data, expected, view.data_length) == 0;
}

static int test_surface_views_fill_and_padded_input(void) {
  static const uint8_t padded[] = {
    1, 2, 3, 4, 5, 6, 7, 8, 99, 99, 99, 99,
    9, 10, 11, 12, 13, 14, 15, 16,
  };
  static const uint8_t expected[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
  bb_const_image_view input = {
    {2, 2, 12, BB_PIXEL_FORMAT_RGBA8_UNORM, BB_COLOR_SPACE_NUMERIC_SRGB, BB_ALPHA_MODE_STRAIGHT},
    padded,
    sizeof(padded),
  };
  bb_image_view mutable_view;
  bb_surface *surface = NULL;
  bb_context *context = NULL;
  BB_TEST_ASSERT(bb_context_create(NULL, &context) == BB_STATUS_OK);
  BB_TEST_ASSERT(bb_surface_create_from_rgba8(context, &input, &surface) == BB_STATUS_OK);
  BB_TEST_ASSERT(surface_equals(surface, 2, 2, expected));
  BB_TEST_ASSERT(bb_surface_get_view(surface, &mutable_view) == BB_STATUS_OK);
  mutable_view.data[0] = 42;
  BB_TEST_ASSERT(bb_surface_clear(surface, (bb_rgba8){10, 20, 30, 0}) == BB_STATUS_OK);
  BB_TEST_ASSERT(mutable_view.data[0] == 10 && mutable_view.data[3] == 0 && mutable_view.data[4] == 10);
  bb_surface_destroy(surface);

  input.desc.row_pitch = 7;
  BB_TEST_ASSERT(bb_surface_create_from_rgba8(context, &input, &surface) == BB_STATUS_INVALID_ARGUMENT);
  BB_TEST_ASSERT(surface == NULL);
  input.desc.row_pitch = 12;
  input.data_length = 19;
  BB_TEST_ASSERT(bb_surface_create_from_rgba8(context, &input, &surface) == BB_STATUS_INVALID_ARGUMENT);
  BB_TEST_ASSERT(bb_surface_create(context, 0, 1, &surface) == BB_STATUS_INVALID_ARGUMENT);
  bb_surface_destroy(NULL);
  bb_context_destroy(context);
  return 1;
}

static int test_surface_allocation_failures_are_clean(void) {
  size_t failure_attempt;
  for (failure_attempt = 2; failure_attempt <= 3; failure_attempt += 1) {
    bb_test_allocator_state state = {0};
    bb_context_desc desc = bb_test_context_desc(&state);
    bb_context *context = NULL;
    bb_surface *surface = (bb_surface *)(uintptr_t)1;
    state.fail_at_attempt = failure_attempt;
    BB_TEST_ASSERT(bb_context_create(&desc, &context) == BB_STATUS_OK);
    BB_TEST_ASSERT(bb_raster_fill(context, 2, 2, (bb_rgba8){1, 2, 3, 4}, &surface) == BB_STATUS_OUT_OF_MEMORY);
    BB_TEST_ASSERT(surface == NULL);
    BB_TEST_ASSERT(bb_context_allocation_bytes(context) == 0);
    bb_context_destroy(context);
    BB_TEST_ASSERT(state.outstanding_allocations == 0);
  }
  return 1;
}

static int test_crop_and_rotation(void) {
  static const uint8_t source_pixels[] = {1, 0, 0, 255, 2, 0, 0, 255, 3, 0, 0, 255, 4, 0, 0, 255, 5, 0, 0, 255, 6, 0, 0, 255};
  static const uint8_t cropped[] = {0, 0, 0, 0, 1, 0, 0, 255, 2, 0, 0, 255, 3, 0, 0, 255};
  static const uint8_t clockwise[] = {4, 0, 0, 255, 1, 0, 0, 255, 5, 0, 0, 255, 2, 0, 0, 255, 6, 0, 0, 255, 3, 0, 0, 255};
  bb_context *context = NULL;
  bb_surface *source = NULL;
  bb_surface *output = NULL;
  bb_surface *rotated = NULL;
  int turn;
  BB_TEST_ASSERT(bb_context_create(NULL, &context) == BB_STATUS_OK);
  BB_TEST_ASSERT(surface_from_bytes(context, 3, 2, source_pixels, &source) == BB_STATUS_OK);
  BB_TEST_ASSERT(bb_raster_crop(context, source, -1, 0, 4, 1, &output) == BB_STATUS_OK);
  BB_TEST_ASSERT(surface_equals(output, 4, 1, cropped));
  bb_surface_destroy(output);
  output = NULL;
  BB_TEST_ASSERT(bb_raster_rotate_quarter_turns(context, source, 1, &output) == BB_STATUS_OK);
  BB_TEST_ASSERT(surface_equals(output, 2, 3, clockwise));
  bb_surface_destroy(output);
  rotated = source;
  for (turn = 0; turn < 4; turn += 1) {
    bb_surface *next = NULL;
    BB_TEST_ASSERT(bb_raster_rotate_quarter_turns(context, rotated, 1, &next) == BB_STATUS_OK);
    if (rotated != source) bb_surface_destroy(rotated);
    rotated = next;
  }
  BB_TEST_ASSERT(surface_equals(rotated, 3, 2, source_pixels));
  bb_surface_destroy(rotated);
  bb_surface_destroy(source);
  bb_context_destroy(context);
  return 1;
}

static int test_alpha_and_color_transforms(void) {
  static const uint8_t pixels[] = {10, 20, 30, 1, 40, 50, 60, 255, 9, 8, 7, 0};
  static const uint8_t opacity_expected[] = {10, 20, 30, 1, 40, 50, 60, 128, 9, 8, 7, 0};
  static const uint8_t invert_expected[] = {10, 20, 30, 254, 40, 50, 60, 0, 9, 8, 7, 255};
  static const uint8_t visible_expected[] = {1, 2, 3, 1, 1, 2, 3, 255, 9, 8, 7, 0};
  bb_context *context = NULL;
  bb_surface *source = NULL;
  bb_surface *output = NULL;
  BB_TEST_ASSERT(bb_context_create(NULL, &context) == BB_STATUS_OK);
  BB_TEST_ASSERT(surface_from_bytes(context, 3, 1, pixels, &source) == BB_STATUS_OK);
  BB_TEST_ASSERT(bb_raster_opacity(context, source, 0.5, &output) == BB_STATUS_OK);
  BB_TEST_ASSERT(surface_equals(output, 3, 1, opacity_expected));
  bb_surface_destroy(output);
  BB_TEST_ASSERT(bb_raster_opacity(context, source, 2.0, &output) == BB_STATUS_INVALID_ARGUMENT);
  BB_TEST_ASSERT(bb_raster_invert_alpha(context, source, &output) == BB_STATUS_OK);
  BB_TEST_ASSERT(surface_equals(output, 3, 1, invert_expected));
  bb_surface_destroy(output);
  BB_TEST_ASSERT(bb_raster_set_visible_rgb(context, source, (bb_rgba8){1, 2, 3, 4}, &output) == BB_STATUS_OK);
  BB_TEST_ASSERT(surface_equals(output, 3, 1, visible_expected));
  bb_surface_destroy(output);
  bb_surface_destroy(source);
  bb_context_destroy(context);
  return 1;
}

static int test_tint_remap_and_shift(void) {
  static const uint8_t pixels[] = {10, 20, 30, 99};
  static const uint8_t tinted[] = {30, 10, 20, 99};
  static const uint8_t remapped[] = {128, 0, 128, 99};
  static const uint8_t shifted[] = {255, 0, 35, 99};
  bb_context *context = NULL;
  bb_surface *source = NULL;
  bb_surface *output = NULL;
  BB_TEST_ASSERT(bb_context_create(NULL, &context) == BB_STATUS_OK);
  BB_TEST_ASSERT(surface_from_bytes(context, 1, 1, pixels, &source) == BB_STATUS_OK);
  BB_TEST_ASSERT(bb_raster_tint_chroma(context, source, (bb_rgba8){255, 0, 128, 255}, &output) == BB_STATUS_OK);
  BB_TEST_ASSERT(surface_equals(output, 1, 1, tinted));
  bb_surface_destroy(output);
  BB_TEST_ASSERT(
    bb_raster_remap_two_color(
      context,
      source,
      (bb_rgba8){20, 30, 40, 255},
      (bb_rgba8){0, 10, 20, 255},
      (bb_rgba8){255, 0, 0, 255},
      (bb_rgba8){0, 0, 255, 255},
      &output
    ) == BB_STATUS_OK
  );
  BB_TEST_ASSERT(surface_equals(output, 1, 1, remapped));
  bb_surface_destroy(output);
  BB_TEST_ASSERT(
    bb_raster_remap_two_color(
      context,
      source,
      (bb_rgba8){1, 1, 1, 255},
      (bb_rgba8){1, 1, 1, 255},
      (bb_rgba8){0, 0, 0, 255},
      (bb_rgba8){0, 0, 0, 255},
      &output
    ) == BB_STATUS_INVALID_ARGUMENT
  );
  BB_TEST_ASSERT(
    bb_raster_shift_rgb(
      context,
      source,
      (bb_rgba8){0, 100, 5, 255},
      (bb_rgba8){250, 0, 10, 255},
      &output
    ) == BB_STATUS_OK
  );
  BB_TEST_ASSERT(surface_equals(output, 1, 1, shifted));
  bb_surface_destroy(output);
  bb_surface_destroy(source);
  bb_context_destroy(context);
  return 1;
}

static int test_source_over_mask_and_canvas(void) {
  static const uint8_t base_pixels[] = {
    0, 0, 255, 128, 0, 0, 255, 128,
    0, 0, 255, 128, 0, 0, 255, 128,
  };
  static const uint8_t source_pixels[] = {255, 0, 0, 128, 255, 0, 0, 128};
  static const uint8_t composite_expected[] = {
    0, 0, 255, 128, 170, 0, 85, 192,
    0, 0, 255, 128, 0, 0, 255, 128,
  };
  static const uint8_t mask_pixels[] = {
    1, 2, 3, 128, 1, 2, 3, 64,
    1, 2, 3, 255, 1, 2, 3, 0,
  };
  static const uint8_t multiplied[] = {
    0, 0, 255, 64, 0, 0, 255, 32,
    0, 0, 255, 128, 0, 0, 255, 0,
  };
  static const uint8_t replaced[] = {
    0, 0, 255, 128, 0, 0, 255, 64,
    0, 0, 255, 255, 0, 0, 255, 0,
  };
  static const uint8_t invisible[] = {9, 8, 7, 0};
  static const uint8_t clear[] = {0, 0, 0, 0};
  bb_context *context = NULL;
  bb_surface *base = NULL;
  bb_surface *source = NULL;
  bb_surface *mask = NULL;
  bb_surface *output = NULL;
  bb_surface *transparent = NULL;
  BB_TEST_ASSERT(bb_context_create(NULL, &context) == BB_STATUS_OK);
  BB_TEST_ASSERT(surface_from_bytes(context, 2, 2, base_pixels, &base) == BB_STATUS_OK);
  BB_TEST_ASSERT(surface_from_bytes(context, 2, 1, source_pixels, &source) == BB_STATUS_OK);
  BB_TEST_ASSERT(bb_raster_source_over(context, base, source, 1, 0, 1.0, &output) == BB_STATUS_OK);
  BB_TEST_ASSERT(surface_equals(output, 2, 2, composite_expected));
  bb_surface_destroy(output);
  BB_TEST_ASSERT(surface_from_bytes(context, 2, 2, mask_pixels, &mask) == BB_STATUS_OK);
  BB_TEST_ASSERT(bb_raster_apply_mask(context, base, mask, BB_MASK_MULTIPLY, &output) == BB_STATUS_OK);
  BB_TEST_ASSERT(surface_equals(output, 2, 2, multiplied));
  bb_surface_destroy(output);
  BB_TEST_ASSERT(bb_raster_apply_mask(context, base, mask, BB_MASK_REPLACE, &output) == BB_STATUS_OK);
  BB_TEST_ASSERT(surface_equals(output, 2, 2, replaced));
  bb_surface_destroy(output);
  BB_TEST_ASSERT(surface_from_bytes(context, 1, 1, invisible, &transparent) == BB_STATUS_OK);
  BB_TEST_ASSERT(bb_raster_canvas(context, transparent, 1, 1, 0, 0, &output) == BB_STATUS_OK);
  BB_TEST_ASSERT(surface_equals(output, 1, 1, clear));
  bb_surface_destroy(output);
  bb_surface_destroy(transparent);
  bb_surface_destroy(mask);
  bb_surface_destroy(source);
  bb_surface_destroy(base);
  bb_context_destroy(context);
  return 1;
}

static int test_alpha_fields_presets_and_gradients(void) {
  static const uint8_t preset_expected[] = {1, 2, 3, 255, 1, 2, 3, 128, 1, 2, 3, 0};
  static const uint8_t linear_expected[] = {0, 0, 0, 0, 128, 128, 128, 128, 255, 255, 255, 255};
  const bb_gradient_stop stops[] = {
    {0.0, {0, 0, 0, 0}, BB_EASING_LINEAR, 0},
    {1.0, {255, 255, 255, 255}, BB_EASING_LINEAR, 0},
  };
  const bb_gradient_stop ellipse_stops[] = {
    {0.0, {255, 255, 255, 255}, BB_EASING_LINEAR, 0},
    {0.5, {255, 0, 0, 128}, BB_EASING_LINEAR, 0},
    {1.0, {0, 0, 0, 0}, BB_EASING_LINEAR, 0},
  };
  bb_linear_gradient_desc linear = {3, 1, 90.0, 0.0, 0, stops, 2, BB_EASING_LINEAR};
  bb_elliptical_gradient_desc ellipse = {5, 5, 2.0, 2.0, 2.0, 1.0, 0.0, ellipse_stops, 3, BB_EASING_LEGACY, 0};
  bb_context *context = NULL;
  bb_surface *output = NULL;
  bb_const_image_view view;
  BB_TEST_ASSERT(bb_context_create(NULL, &context) == BB_STATUS_OK);
  BB_TEST_ASSERT(
    bb_raster_preset_gradient(context, 3, 1, BB_GRADIENT_LEFT_RIGHT, 0, (bb_rgba8){1, 2, 3, 255}, &output) ==
    BB_STATUS_OK
  );
  BB_TEST_ASSERT(surface_equals(output, 3, 1, preset_expected));
  bb_surface_destroy(output);
  BB_TEST_ASSERT(bb_raster_linear_gradient(context, &linear, &output) == BB_STATUS_OK);
  BB_TEST_ASSERT(surface_equals(output, 3, 1, linear_expected));
  bb_surface_destroy(output);
  ellipse.rotation_radians = acos(-1.0) / 2.0;
  BB_TEST_ASSERT(bb_raster_elliptical_gradient(context, &ellipse, &output) == BB_STATUS_OK);
  BB_TEST_ASSERT(bb_surface_get_const_view(output, &view) == BB_STATUS_OK);
  BB_TEST_ASSERT(memcmp(view.data + (2 * 5 + 2) * 4, (uint8_t[]){255, 255, 255, 255}, 4) == 0);
  BB_TEST_ASSERT(memcmp(view.data + 2 * 4, (uint8_t[]){0, 0, 0, 0}, 4) == 0);
  BB_TEST_ASSERT(memcmp(view.data + (1 * 5 + 2) * 4, (uint8_t[]){255, 0, 0, 128}, 4) == 0);
  bb_surface_destroy(output);
  bb_context_destroy(context);
  return 1;
}

static int test_gradient_allocation_failures_are_clean(void) {
  const bb_gradient_stop stops[] = {
    {0.0, {0, 0, 0, 255}, BB_EASING_LINEAR, 0},
    {1.0, {255, 255, 255, 255}, BB_EASING_LINEAR, 0},
  };
  const bb_linear_gradient_desc gradient = {2, 2, 180.0, 0.0, 0, stops, 2, BB_EASING_LINEAR};
  size_t failure_attempt;
  for (failure_attempt = 2; failure_attempt <= 4; failure_attempt += 1) {
    bb_test_allocator_state state = {0};
    bb_context_desc desc = bb_test_context_desc(&state);
    bb_context *context = NULL;
    bb_surface *output = (bb_surface *)(uintptr_t)1;
    state.fail_at_attempt = failure_attempt;
    BB_TEST_ASSERT(bb_context_create(&desc, &context) == BB_STATUS_OK);
    BB_TEST_ASSERT(bb_raster_linear_gradient(context, &gradient, &output) == BB_STATUS_OUT_OF_MEMORY);
    BB_TEST_ASSERT(output == NULL);
    BB_TEST_ASSERT(bb_context_allocation_bytes(context) == 0);
    bb_context_destroy(context);
    BB_TEST_ASSERT(state.outstanding_allocations == 0);
  }
  return 1;
}

static int test_reference_alpha_maps_match_archive_contract(void) {
  static const int32_t offsets[18][2] = {
    {0, 0}, {0, 0}, {0, 0}, {1, 0}, {1, 1}, {0, 0}, {1, 1}, {0, 0}, {1, 1},
    {0, 0}, {0, 1}, {0, 0}, {1, 0}, {1, 1}, {0, 0}, {0, 0}, {1, 1}, {1, 1},
  };
  bb_context *context = NULL;
  uint32_t index;
  BB_TEST_ASSERT(bb_context_create(NULL, &context) == BB_STATUS_OK);
  for (index = 0; index < 18; index += 1) {
    char path[128];
    bb_png_fixture fixture;
    bb_reference_alpha_map map;
    bb_const_image_view view;
    size_t pixel;
    BB_TEST_ASSERT(snprintf(path, sizeof(path), "reference-set/Gradient Layers Alpha Maps/%02u.png", index) > 0);
    BB_TEST_ASSERT(bb_png_fixture_load(path, &fixture));
    BB_TEST_ASSERT(bb_reference_set_alpha_map(context, index, NULL, &map) == BB_STATUS_OK);
    BB_TEST_ASSERT(bb_surface_get_const_view(map.surface, &view) == BB_STATUS_OK);
    BB_TEST_ASSERT(view.desc.width == fixture.width && view.desc.height == fixture.height);
    BB_TEST_ASSERT(map.offset_x == offsets[index][0] && map.offset_y == offsets[index][1]);
    BB_TEST_ASSERT(map.raster_fallback == 0);
    for (pixel = 0; pixel < (size_t)fixture.width * fixture.height; pixel += 1) {
      BB_TEST_ASSERT(view.data[pixel * 4 + 3] == fixture.rgba[pixel * 4 + 3]);
    }
    bb_surface_destroy(map.surface);
    bb_png_fixture_destroy(&fixture);
  }
  bb_context_destroy(context);
  return 1;
}

static int test_flat_color_matches_reference_fixture(void) {
  bb_png_fixture fixture;
  bb_context *context = NULL;
  bb_surface *surface = NULL;
  bb_const_image_view view;
  BB_TEST_ASSERT(bb_png_fixture_load("reference-set/col/col_blue_hi.png", &fixture));
  BB_TEST_ASSERT(bb_context_create(NULL, &context) == BB_STATUS_OK);
  BB_TEST_ASSERT(bb_raster_fill(context, fixture.width, fixture.height, (bb_rgba8){0, 0, 255, 255}, &surface) == BB_STATUS_OK);
  BB_TEST_ASSERT(bb_surface_get_const_view(surface, &view) == BB_STATUS_OK);
  BB_TEST_ASSERT(view.data_length == fixture.rgba_length && memcmp(view.data, fixture.rgba, fixture.rgba_length) == 0);
  bb_surface_destroy(surface);
  bb_context_destroy(context);
  bb_png_fixture_destroy(&fixture);
  return 1;
}

static int test_reference_map_18_is_explicit_raster_fallback(void) {
  bb_png_fixture fixture;
  bb_const_image_view fixture_view;
  bb_context *context = NULL;
  bb_surface *raster = NULL;
  bb_reference_alpha_map map;
  bb_const_image_view result;
  BB_TEST_ASSERT(bb_png_fixture_load("reference-set/Gradient Layers Alpha Maps/18.png", &fixture));
  fixture_view.desc = (bb_image_desc){
    fixture.width,
    fixture.height,
    (size_t)fixture.width * 4,
    BB_PIXEL_FORMAT_RGBA8_UNORM,
    BB_COLOR_SPACE_NUMERIC_SRGB,
    BB_ALPHA_MODE_STRAIGHT,
  };
  fixture_view.data = fixture.rgba;
  fixture_view.data_length = fixture.rgba_length;
  BB_TEST_ASSERT(bb_context_create(NULL, &context) == BB_STATUS_OK);
  BB_TEST_ASSERT(bb_reference_set_alpha_map(context, 18, NULL, &map) == BB_STATUS_NOT_FOUND);
  BB_TEST_ASSERT(bb_surface_create_from_rgba8(context, &fixture_view, &raster) == BB_STATUS_OK);
  BB_TEST_ASSERT(bb_reference_set_alpha_map(context, 18, raster, &map) == BB_STATUS_OK);
  BB_TEST_ASSERT(map.raster_fallback == 1 && map.offset_x == 0 && map.offset_y == 0);
  BB_TEST_ASSERT(bb_surface_get_const_view(map.surface, &result) == BB_STATUS_OK);
  BB_TEST_ASSERT(result.data_length == fixture.rgba_length && memcmp(result.data, fixture.rgba, fixture.rgba_length) == 0);
  bb_surface_destroy(map.surface);
  bb_surface_destroy(raster);
  bb_context_destroy(context);
  bb_png_fixture_destroy(&fixture);
  return 1;
}

static int test_lanczos3_alpha_and_reference_fixture(void) {
  static const uint8_t transparent_source[] = {
    255, 0, 0, 255, 0, 255, 0, 0,
    0, 0, 255, 0, 255, 255, 255, 0,
  };
  bb_png_fixture large;
  bb_png_fixture small;
  bb_const_image_view large_view;
  bb_context *context = NULL;
  bb_surface *source = NULL;
  bb_surface *output = NULL;
  bb_const_image_view result;
  size_t index;
  unsigned maximum_error = 0;
  BB_TEST_ASSERT(bb_context_create(NULL, &context) == BB_STATUS_OK);
  BB_TEST_ASSERT(surface_from_bytes(context, 2, 2, transparent_source, &source) == BB_STATUS_OK);
  BB_TEST_ASSERT(bb_raster_resize_lanczos3(context, source, 1, 1, &output) == BB_STATUS_OK);
  BB_TEST_ASSERT(bb_surface_get_const_view(output, &result) == BB_STATUS_OK);
  BB_TEST_ASSERT(result.data[3] > 0);
  BB_TEST_ASSERT(result.data[0] > 0 && result.data[2] == 0);
  bb_surface_destroy(output);
  bb_surface_destroy(source);

  BB_TEST_ASSERT(bb_png_fixture_load("reference-set/col bin 2/rgb/col_blue_hi-grad00blk100.png", &large));
  BB_TEST_ASSERT(bb_png_fixture_load("reference-set/blue 64-8 24 bit/col_blue_hi-grad00blk100.png", &small));
  large_view.desc = (bb_image_desc){
    large.width,
    large.height,
    (size_t)large.width * 4,
    BB_PIXEL_FORMAT_RGBA8_UNORM,
    BB_COLOR_SPACE_NUMERIC_SRGB,
    BB_ALPHA_MODE_STRAIGHT,
  };
  large_view.data = large.rgba;
  large_view.data_length = large.rgba_length;
  BB_TEST_ASSERT(bb_surface_create_from_rgba8(context, &large_view, &source) == BB_STATUS_OK);
  BB_TEST_ASSERT(bb_raster_resize_lanczos3(context, source, small.width, small.height, &output) == BB_STATUS_OK);
  BB_TEST_ASSERT(bb_surface_get_const_view(output, &result) == BB_STATUS_OK);
  BB_TEST_ASSERT(result.data_length == small.rgba_length);
  for (index = 0; index < result.data_length; index += 1) {
    const unsigned error = result.data[index] > small.rgba[index] ? result.data[index] - small.rgba[index] : small.rgba[index] - result.data[index];
    if (error > maximum_error) maximum_error = error;
  }
  BB_TEST_ASSERT(maximum_error <= 2);
  bb_surface_destroy(output);
  bb_surface_destroy(source);
  bb_png_fixture_destroy(&large);
  bb_png_fixture_destroy(&small);
  bb_context_destroy(context);
  return 1;
}

static int test_lanczos_allocation_failures_are_clean(void) {
  static const uint8_t pixels[] = {1, 2, 3, 255, 4, 5, 6, 255, 7, 8, 9, 255, 10, 11, 12, 255};
  size_t failure_offset;
  for (failure_offset = 1; failure_offset <= 3; failure_offset += 1) {
    bb_test_allocator_state state = {0};
    bb_context_desc desc = bb_test_context_desc(&state);
    bb_context *context = NULL;
    bb_surface *source = NULL;
    bb_surface *output = (bb_surface *)(uintptr_t)1;
    BB_TEST_ASSERT(bb_context_create(&desc, &context) == BB_STATUS_OK);
    BB_TEST_ASSERT(surface_from_bytes(context, 2, 2, pixels, &source) == BB_STATUS_OK);
    state.fail_at_attempt = state.attempt_count + failure_offset;
    BB_TEST_ASSERT(bb_raster_resize_lanczos3(context, source, 1, 1, &output) == BB_STATUS_OUT_OF_MEMORY);
    BB_TEST_ASSERT(output == NULL);
    bb_surface_destroy(source);
    BB_TEST_ASSERT(bb_context_allocation_bytes(context) == 0);
    bb_context_destroy(context);
    BB_TEST_ASSERT(state.outstanding_allocations == 0);
  }
  return 1;
}

const bb_test_case bb_raster_tests[] = {
  {"surface views, fill, and padded input", test_surface_views_fill_and_padded_input},
  {"surface allocation failures", test_surface_allocation_failures_are_clean},
  {"crop and quarter-turn rotation", test_crop_and_rotation},
  {"alpha and visible-RGB transforms", test_alpha_and_color_transforms},
  {"tint, remap, and RGB shift", test_tint_remap_and_shift},
  {"source-over, masks, and canvas", test_source_over_mask_and_canvas},
  {"alpha fields, presets, and gradients", test_alpha_fields_presets_and_gradients},
  {"gradient allocation failures", test_gradient_allocation_failures_are_clean},
  {"reference alpha maps 00-17", test_reference_alpha_maps_match_archive_contract},
  {"flat color reference fixture", test_flat_color_matches_reference_fixture},
  {"reference alpha map 18 fallback", test_reference_map_18_is_explicit_raster_fallback},
  {"Lanczos3 alpha and reference fixture", test_lanczos3_alpha_and_reference_fixture},
  {"Lanczos3 allocation failures", test_lanczos_allocation_failures_are_clean},
};

const size_t bb_raster_test_count = sizeof(bb_raster_tests) / sizeof(bb_raster_tests[0]);
