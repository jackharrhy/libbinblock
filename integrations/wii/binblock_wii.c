#include "binblock_wii.h"

#include <stdlib.h>
#include <string.h>

struct bb_wii_program {
  bb_context *context;
  bb_program *program;
  bb_allocator allocator;
  uint32_t uses_host_allocator;
};

bb_status bb_wii_rgba8_texture_measure(
  uint32_t width,
  uint32_t height,
  bb_wii_texture_desc *out_desc
) {
  uint64_t padded_width;
  uint64_t padded_height;
  uint64_t bytes;
  if (out_desc == NULL || width == 0 || height == 0) return BB_STATUS_INVALID_ARGUMENT;
  padded_width = ((uint64_t)width + 3) & ~UINT64_C(3);
  padded_height = ((uint64_t)height + 3) & ~UINT64_C(3);
  bytes = padded_width * padded_height * 4;
  if (padded_width > UINT32_MAX || padded_height > UINT32_MAX || bytes > SIZE_MAX)
    return BB_STATUS_OVERFLOW;
  *out_desc = (bb_wii_texture_desc){
    width,
    height,
    (uint32_t)padded_width,
    (uint32_t)padded_height,
    (size_t)bytes,
  };
  return BB_STATUS_OK;
}

bb_status bb_wii_rgba8_texture_encode(
  bb_const_image_view source,
  uint8_t *destination,
  size_t capacity,
  bb_wii_texture_desc *out_desc
) {
  bb_wii_texture_desc desc;
  uint32_t tile_y;
  uint32_t tile_x;
  size_t destination_offset = 0;
  bb_status status = bb_wii_rgba8_texture_measure(source.desc.width, source.desc.height, &desc);
  if (status != BB_STATUS_OK) return status;
  if (out_desc == NULL || destination == NULL || capacity < desc.byte_length || source.data == NULL ||
      source.desc.format != BB_PIXEL_FORMAT_RGBA8_UNORM ||
      source.desc.color_space != BB_COLOR_SPACE_NUMERIC_SRGB ||
      source.desc.alpha_mode != BB_ALPHA_MODE_STRAIGHT ||
      source.desc.row_pitch < (size_t)source.desc.width * 4 ||
      source.data_length < source.desc.row_pitch * source.desc.height) return BB_STATUS_INVALID_ARGUMENT;
  memset(destination, 0, desc.byte_length);
  for (tile_y = 0; tile_y < desc.padded_height; tile_y += 4) {
    for (tile_x = 0; tile_x < desc.padded_width; tile_x += 4) {
      uint32_t pixel;
      for (pixel = 0; pixel < 16; pixel += 1) {
        const uint32_t x = tile_x + pixel % 4;
        const uint32_t y = tile_y + pixel / 4;
        const uint8_t *rgba = x < source.desc.width && y < source.desc.height
                                ? source.data + (size_t)y * source.desc.row_pitch + (size_t)x * 4
                                : NULL;
        destination[destination_offset + pixel * 2] = rgba == NULL ? 0 : rgba[3];
        destination[destination_offset + pixel * 2 + 1] = rgba == NULL ? 0 : rgba[0];
        destination[destination_offset + 32 + pixel * 2] = rgba == NULL ? 0 : rgba[1];
        destination[destination_offset + 32 + pixel * 2 + 1] = rgba == NULL ? 0 : rgba[2];
      }
      destination_offset += 64;
    }
  }
  *out_desc = desc;
  return BB_STATUS_OK;
}

bb_status bb_wii_program_load_source(
  const bb_context_desc *context_desc,
  bb_bytes source,
  const bb_compile_options *options,
  bb_wii_program **out_program
) {
  bb_wii_program *host = NULL;
  bb_context *context = NULL;
  bb_source_id source_id = BB_SOURCE_ID_NONE;
  bb_syntax_tree *syntax = NULL;
  bb_status status;
  if (out_program == NULL) return BB_STATUS_INVALID_ARGUMENT;
  *out_program = NULL;
  status = bb_context_create(context_desc, &context);
  if (status != BB_STATUS_OK) return status;
  if (context_desc != NULL && context_desc->allocator.alloc != NULL) {
    host = context_desc->allocator.alloc(
      context_desc->allocator.user,
      sizeof(*host),
      _Alignof(bb_wii_program)
    );
    if (host != NULL) {
      memset(host, 0, sizeof(*host));
      host->allocator = context_desc->allocator;
      host->uses_host_allocator = 1;
    }
  } else host = calloc(1, sizeof(*host));
  if (host == NULL) {
    bb_context_destroy(context);
    return BB_STATUS_OUT_OF_MEMORY;
  }
  host->context = context;
  if (status == BB_STATUS_OK)
    status = bb_context_add_source(
      host->context,
      (bb_string_view){"program.binscript", sizeof("program.binscript") - 1},
      source,
      &source_id
    );
  if (status == BB_STATUS_OK) status = bb_syntax_parse(host->context, source_id, &syntax);
  if (status == BB_STATUS_OK)
    status = bb_program_compile_with_options(host->context, syntax, options, &host->program);
  bb_syntax_tree_destroy(syntax);
  if (status != BB_STATUS_OK) {
    bb_wii_program_destroy(host);
    return status;
  }
  *out_program = host;
  return BB_STATUS_OK;
}

void bb_wii_program_destroy(bb_wii_program *program) {
  bb_allocator allocator;
  uint32_t uses_host_allocator;
  if (program == NULL) return;
  allocator = program->allocator;
  uses_host_allocator = program->uses_host_allocator;
  bb_program_destroy(program->program);
  bb_context_destroy(program->context);
  if (uses_host_allocator)
    allocator.free(allocator.user, program, sizeof(*program), _Alignof(bb_wii_program));
  else free(program);
}

size_t bb_wii_program_diagnostic_count(const bb_wii_program *program) {
  return program == NULL ? 0 : bb_program_diagnostic_count(program->program);
}

bb_status bb_wii_program_diagnostic(
  const bb_wii_program *program,
  size_t index,
  bb_diagnostic *out_diagnostic
) {
  if (program == NULL) return BB_STATUS_INVALID_ARGUMENT;
  return bb_program_diagnostic(program->program, index, out_diagnostic);
}

size_t bb_wii_program_output_count(const bb_wii_program *program) {
  return program == NULL ? 0 : bb_program_output_count(program->program);
}

bb_status bb_wii_program_output(
  const bb_wii_program *program,
  size_t output_index,
  bb_program_output_info *out_info
) {
  if (program == NULL) return BB_STATUS_INVALID_ARGUMENT;
  return bb_program_output(program->program, output_index, out_info);
}

bb_status bb_wii_program_measure_output_texture(
  const bb_wii_program *program,
  size_t output_index,
  uint64_t item_index,
  bb_wii_texture_desc *out_desc
) {
  bb_artifact_value artifact;
  uint32_t width;
  uint32_t height;
  bb_status status;
  if (program == NULL || out_desc == NULL) return BB_STATUS_INVALID_ARGUMENT;
  status = bb_program_output_artifact(program->program, output_index, item_index, &artifact);
  if (status == BB_STATUS_OK)
    status = bb_image_graph_node_dimensions(
      bb_program_image_graph(program->program),
      artifact.image,
      &width,
      &height
    );
  return status == BB_STATUS_OK ? bb_wii_rgba8_texture_measure(width, height, out_desc) : status;
}

bb_status bb_wii_program_render_output_texture(
  bb_wii_program *program,
  size_t output_index,
  uint64_t item_index,
  uint8_t *scratch,
  size_t scratch_capacity,
  bb_wii_texture_desc *out_desc
) {
  bb_wii_texture_desc required;
  bb_surface *surface = NULL;
  bb_const_image_view view;
  bb_status status;
  if (program == NULL || scratch == NULL || out_desc == NULL) return BB_STATUS_INVALID_ARGUMENT;
  status = bb_wii_program_measure_output_texture(program, output_index, item_index, &required);
  if (status != BB_STATUS_OK) return status;
  if (scratch_capacity < required.byte_length) return BB_STATUS_LIMIT_EXCEEDED;
  status = bb_program_render_output(program->program, output_index, item_index, &surface);
  if (status == BB_STATUS_OK) status = bb_surface_get_const_view(surface, &view);
  if (status == BB_STATUS_OK)
    status = bb_wii_rgba8_texture_encode(view, scratch, scratch_capacity, out_desc);
  bb_surface_destroy(surface);
  return status;
}

bb_status bb_wii_program_render_and_upload(
  bb_wii_program *program,
  size_t output_index,
  uint64_t item_index,
  uint8_t *scratch,
  size_t scratch_capacity,
  bb_wii_texture_upload_fn upload,
  void *user,
  bb_wii_texture_desc *out_desc
) {
  bb_status status;
  if (upload == NULL || out_desc == NULL) return BB_STATUS_INVALID_ARGUMENT;
  status = bb_wii_program_render_output_texture(
    program,
    output_index,
    item_index,
    scratch,
    scratch_capacity,
    out_desc
  );
  if (status != BB_STATUS_OK) return status;
  return upload(user, out_desc, scratch, out_desc->byte_length);
}
