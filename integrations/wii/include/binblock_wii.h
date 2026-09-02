#ifndef BINBLOCK_WII_H
#define BINBLOCK_WII_H

#include <binblock/program.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct bb_wii_texture_desc {
  uint32_t width;
  uint32_t height;
  uint32_t padded_width;
  uint32_t padded_height;
  size_t byte_length;
} bb_wii_texture_desc;

typedef struct bb_wii_program bb_wii_program;

typedef bb_status (*bb_wii_texture_upload_fn)(
  void *user,
  const bb_wii_texture_desc *texture,
  const uint8_t *gx_rgba8,
  size_t byte_length
);

/* Converts canonical straight-alpha RGBA8 rows into GX_TF_RGBA8 4x4 tiles.
 * Each tile contains the 32-byte AR plane followed by the 32-byte GB plane. */
bb_status bb_wii_rgba8_texture_measure(
  uint32_t width,
  uint32_t height,
  bb_wii_texture_desc *out_desc
);
bb_status bb_wii_rgba8_texture_encode(
  bb_const_image_view source,
  uint8_t *destination,
  size_t capacity,
  bb_wii_texture_desc *out_desc
);

/* Loads UTF-8 BinScript without filesystem access.
 * Resolver callbacks and their user pointer in options must outlive the Wii
 * program when imported modules or assets are used during later rendering. */
bb_status bb_wii_program_load_source(
  const bb_context_desc *context_desc,
  bb_bytes source,
  const bb_compile_options *options,
  bb_wii_program **out_program
);
void bb_wii_program_destroy(bb_wii_program *program);
size_t bb_wii_program_diagnostic_count(const bb_wii_program *program);
bb_status bb_wii_program_diagnostic(
  const bb_wii_program *program,
  size_t index,
  bb_diagnostic *out_diagnostic
);
size_t bb_wii_program_output_count(const bb_wii_program *program);
bb_status bb_wii_program_output(
  const bb_wii_program *program,
  size_t output_index,
  bb_program_output_info *out_info
);
bb_status bb_wii_program_measure_output_texture(
  const bb_wii_program *program,
  size_t output_index,
  uint64_t item_index,
  bb_wii_texture_desc *out_desc
);
bb_status bb_wii_program_render_output_texture(
  bb_wii_program *program,
  size_t output_index,
  uint64_t item_index,
  uint8_t *scratch,
  size_t scratch_capacity,
  bb_wii_texture_desc *out_desc
);
bb_status bb_wii_program_render_and_upload(
  bb_wii_program *program,
  size_t output_index,
  uint64_t item_index,
  uint8_t *scratch,
  size_t scratch_capacity,
  bb_wii_texture_upload_fn upload,
  void *user,
  bb_wii_texture_desc *out_desc
);

#ifdef __cplusplus
}
#endif

#endif
