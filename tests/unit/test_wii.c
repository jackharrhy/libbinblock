#include "test_support.h"

#include "binblock_wii.h"

#include <string.h>

#include "../../integrations/wii/demo/source/suite_source.h"

typedef struct wii_upload_state {
  size_t calls;
  size_t bytes;
  uint8_t first[4];
} wii_upload_state;

static bb_status wii_test_upload(
  void *user,
  const bb_wii_texture_desc *texture,
  const uint8_t *gx_rgba8,
  size_t byte_length
) {
  wii_upload_state *state = user;
  if (texture == NULL || gx_rgba8 == NULL || byte_length != texture->byte_length) return BB_STATUS_INVALID_ARGUMENT;
  state->calls += 1;
  state->bytes = byte_length;
  memcpy(state->first, gx_rgba8, sizeof(state->first));
  return BB_STATUS_OK;
}

static int test_wii_rgba8_tile_layout(void) {
  const uint8_t pixels[] = {0x11, 0x22, 0x33, 0x44};
  const bb_const_image_view source = {
    {1, 1, 4, BB_PIXEL_FORMAT_RGBA8_UNORM, BB_COLOR_SPACE_NUMERIC_SRGB, BB_ALPHA_MODE_STRAIGHT},
    pixels,
    sizeof(pixels),
  };
  bb_wii_texture_desc desc;
  uint8_t encoded[64];
  BB_TEST_ASSERT(bb_wii_rgba8_texture_measure(1, 1, &desc) == BB_STATUS_OK);
  BB_TEST_ASSERT(desc.padded_width == 4 && desc.padded_height == 4 && desc.byte_length == 64);
  BB_TEST_ASSERT(bb_wii_rgba8_texture_encode(source, encoded, sizeof(encoded), &desc) == BB_STATUS_OK);
  BB_TEST_ASSERT(encoded[0] == 0x44 && encoded[1] == 0x11);
  BB_TEST_ASSERT(encoded[32] == 0x22 && encoded[33] == 0x33);
  BB_TEST_ASSERT(encoded[2] == 0 && encoded[34] == 0);
  return 1;
}

static int test_wii_host_loads_source_and_uploads_baked_gx_texture(void) {
  static const char source[] =
    "import \"binblock/basic\"\n"
    "fill(#11223344).size(4)\n";
  bb_wii_program *program = NULL;
  bb_program_output_info output;
  bb_wii_texture_desc texture;
  uint8_t scratch[64];
  wii_upload_state upload = {0};
  BB_TEST_ASSERT(
    bb_wii_program_load_source(
      NULL,
      (bb_bytes){(const uint8_t *)source, sizeof(source) - 1},
      NULL,
      &program
    ) == BB_STATUS_OK
  );
  BB_TEST_ASSERT(bb_wii_program_diagnostic_count(program) == 0);
  BB_TEST_ASSERT(bb_wii_program_output_count(program) == 1);
  BB_TEST_ASSERT(bb_wii_program_output(program, 0, &output) == BB_STATUS_OK && output.cardinality == 1);
  BB_TEST_ASSERT(bb_wii_program_measure_output_texture(program, 0, 0, &texture) == BB_STATUS_OK);
  BB_TEST_ASSERT(texture.width == 4 && texture.height == 4 && texture.byte_length == sizeof(scratch));
  BB_TEST_ASSERT(
    bb_wii_program_render_and_upload(
      program,
      0,
      0,
      scratch,
      sizeof(scratch),
      wii_test_upload,
      &upload,
      &texture
    ) == BB_STATUS_OK
  );
  BB_TEST_ASSERT(upload.calls == 1 && upload.bytes == 64);
  BB_TEST_ASSERT(upload.first[0] == 0x44 && upload.first[1] == 0x11);
  bb_wii_program_destroy(program);
  return 1;
}

static int test_wii_demo_suite_source_is_valid_and_varied(void) {
  bb_wii_program *program = NULL;
  bb_program_output_info output;
  bb_wii_texture_desc texture;
  uint8_t scratch[64 * 64 * 4];
  wii_upload_state upload = {0};
  BB_TEST_ASSERT(
    bb_wii_program_load_source(
      NULL,
      (bb_bytes){bb_wii_demo_source, BB_WII_DEMO_SOURCE_BYTES},
      NULL,
      &program
    ) == BB_STATUS_OK
  );
  BB_TEST_ASSERT(bb_wii_program_diagnostic_count(program) == 0);
  BB_TEST_ASSERT(bb_wii_program_output_count(program) == 1);
  BB_TEST_ASSERT(bb_wii_program_output(program, 0, &output) == BB_STATUS_OK);
  BB_TEST_ASSERT(output.item_type == BB_SEMANTIC_ARTIFACT && output.cardinality == 12);

  BB_TEST_ASSERT(bb_wii_program_measure_output_texture(program, 0, 0, &texture) == BB_STATUS_OK);
  BB_TEST_ASSERT(texture.width == 64 && texture.height == 64 && texture.byte_length == sizeof(scratch));
  BB_TEST_ASSERT(
    bb_wii_program_render_and_upload(
      program,
      0,
      0,
      scratch,
      sizeof(scratch),
      wii_test_upload,
      &upload,
      &texture
    ) == BB_STATUS_OK
  );
  BB_TEST_ASSERT(upload.calls == 1 && upload.bytes == sizeof(scratch));

  BB_TEST_ASSERT(bb_wii_program_measure_output_texture(program, 0, 4, &texture) == BB_STATUS_OK);
  BB_TEST_ASSERT(texture.width == 72 && texture.height == 48);
  BB_TEST_ASSERT(bb_wii_program_measure_output_texture(program, 0, 5, &texture) == BB_STATUS_OK);
  BB_TEST_ASSERT(texture.width == 48 && texture.height == 72);
  BB_TEST_ASSERT(bb_wii_program_measure_output_texture(program, 0, 8, &texture) == BB_STATUS_OK);
  BB_TEST_ASSERT(texture.width == 80 && texture.height == 32);
  BB_TEST_ASSERT(bb_wii_program_measure_output_texture(program, 0, 10, &texture) == BB_STATUS_OK);
  BB_TEST_ASSERT(texture.width == 56 && texture.height == 72);
  BB_TEST_ASSERT(bb_wii_program_measure_output_texture(program, 0, 11, &texture) == BB_STATUS_OK);
  BB_TEST_ASSERT(texture.width == 64 && texture.height == 64);
  bb_wii_program_destroy(program);
  return 1;
}

const bb_test_case bb_wii_tests[] = {
  {"Wii RGBA8 tile layout", test_wii_rgba8_tile_layout},
  {"Wii host loads source and uploads baked GX texture", test_wii_host_loads_source_and_uploads_baked_gx_texture},
  {"Wii demo suite source is valid and varied", test_wii_demo_suite_source_is_valid_and_varied},
};

const size_t bb_wii_test_count = sizeof(bb_wii_tests) / sizeof(bb_wii_tests[0]);
