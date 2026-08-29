#include <binblock/raster.h>

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  bb_context *context = NULL;
  bb_surface *original = NULL;
  bb_surface *rotated[4] = {NULL, NULL, NULL, NULL};
  bb_surface *cropped = NULL;
  bb_surface *canvas = NULL;
  bb_surface *resized = NULL;
  bb_const_image_view before;
  bb_const_image_view after;
  const uint32_t width = size > 0 ? (data[0] % 24) + 1 : 1;
  const uint32_t height = size > 1 ? (data[1] % 24) + 1 : 1;
  const uint32_t crop_width = size > 2 ? (data[2] % 24) + 1 : 1;
  const uint32_t crop_height = size > 3 ? (data[3] % 24) + 1 : 1;
  const int32_t x = size > 4 ? (int8_t)data[4] : 0;
  const int32_t y = size > 5 ? (int8_t)data[5] : 0;
  const bb_rgba8 color = {
    size > 6 ? data[6] : 0,
    size > 7 ? data[7] : 0,
    size > 8 ? data[8] : 0,
    size > 9 ? data[9] : 255,
  };
  if (bb_context_create(NULL, &context) != BB_STATUS_OK ||
      bb_raster_fill(context, width, height, color, &original) != BB_STATUS_OK ||
      bb_raster_rotate_quarter_turns(context, original, 1, &rotated[0]) != BB_STATUS_OK ||
      bb_raster_rotate_quarter_turns(context, rotated[0], 1, &rotated[1]) != BB_STATUS_OK ||
      bb_raster_rotate_quarter_turns(context, rotated[1], 1, &rotated[2]) != BB_STATUS_OK ||
      bb_raster_rotate_quarter_turns(context, rotated[2], 1, &rotated[3]) != BB_STATUS_OK)
    goto cleanup;
  if (bb_surface_get_const_view(original, &before) != BB_STATUS_OK ||
      bb_surface_get_const_view(rotated[3], &after) != BB_STATUS_OK ||
      before.desc.width != after.desc.width || before.desc.height != after.desc.height ||
      before.data_length != after.data_length || memcmp(before.data, after.data, before.data_length) != 0)
    abort();
  (void)bb_raster_crop(context, original, x, y, crop_width, crop_height, &cropped);
  if (cropped != NULL)
    (void)bb_raster_canvas(context, cropped, width, height, -x, -y, &canvas);
  (void)bb_raster_resize_lanczos3(context, original, crop_width, crop_height, &resized);

cleanup:
  bb_surface_destroy(resized);
  bb_surface_destroy(canvas);
  bb_surface_destroy(cropped);
  bb_surface_destroy(rotated[3]);
  bb_surface_destroy(rotated[2]);
  bb_surface_destroy(rotated[1]);
  bb_surface_destroy(rotated[0]);
  bb_surface_destroy(original);
  bb_context_destroy(context);
  return 0;
}
