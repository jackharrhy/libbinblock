#include "binblock_sdl_gpu.h"

#include <SDL3/SDL.h>
#include <binblock/program.h>
#include <binblock/syntax.h>

#include <stdio.h>
#include <stdlib.h>

static uint8_t *read_file(const char *path, size_t *out_size) {
  FILE *file = fopen(path, "rb");
  long length;
  uint8_t *data;
  if (file == NULL) return NULL;
  if (fseek(file, 0, SEEK_END) != 0 || (length = ftell(file)) < 0 || fseek(file, 0, SEEK_SET) != 0) {
    fclose(file);
    return NULL;
  }
  data = malloc((size_t)length + 1);
  if (data == NULL) {
    fclose(file);
    return NULL;
  }
  if (fread(data, 1, (size_t)length, file) != (size_t)length) {
    fclose(file);
    free(data);
    return NULL;
  }
  fclose(file);
  data[length] = 0;
  *out_size = (size_t)length;
  return data;
}

static void print_program_diagnostics(const bb_program *program) {
  size_t index;
  for (index = 0; index < bb_program_diagnostic_count(program); index += 1) {
    bb_diagnostic diagnostic;
    if (bb_program_diagnostic(program, index, &diagnostic) == BB_STATUS_OK)
      fprintf(stderr, "%.*s\n", (int)diagnostic.message.length, diagnostic.message.data);
  }
}

int main(int argc, char **argv) {
  enum { columns = 16, rows = 16, tile_size = 64, atlas_width = columns * tile_size, atlas_height = rows * tile_size };
  const char *source_path = argc > 1 ? argv[1] : "examples/generated-set.binscript";
  uint8_t *source = NULL;
  size_t source_size = 0;
  bb_context *context = NULL;
  bb_source_id source_id = BB_SOURCE_ID_NONE;
  bb_syntax_tree *syntax = NULL;
  bb_program *program = NULL;
  bb_sdl_gpu_item *items = NULL;
  bb_sdl_gpu_renderer *renderer = NULL;
  bb_sdl_gpu_batch *batch = NULL;
  SDL_GPUDevice *device = NULL;
  SDL_GPUTexture *texture = NULL;
  SDL_GPUTransferBuffer *download = NULL;
  SDL_GPUCommandBuffer *commands = NULL;
  SDL_GPUFence *fence = NULL;
  uint8_t *pixels = NULL;
  size_t item_index;
  size_t mismatch_count = 0;
  size_t mismatch_examples = 0;
  unsigned max_error = 0;
  int result = EXIT_FAILURE;
  SDL_GPUShaderFormat shader_formats = SDL_GPU_SHADERFORMAT_SPIRV;

  source = read_file(source_path, &source_size);
  if (source == NULL) {
    fprintf(stderr, "could not read %s\n", source_path);
    goto cleanup;
  }
  if (bb_context_create(NULL, &context) != BB_STATUS_OK ||
      bb_context_add_source(
        context,
        (bb_string_view){source_path, strlen(source_path)},
        (bb_bytes){source, source_size},
        &source_id
      ) != BB_STATUS_OK ||
      bb_syntax_parse(context, source_id, &syntax) != BB_STATUS_OK ||
      bb_program_compile(context, syntax, &program) != BB_STATUS_OK) {
    fprintf(stderr, "could not compile generated set\n");
    goto cleanup;
  }
  print_program_diagnostics(program);
  if (bb_program_output_count(program) < 3) {
    fprintf(stderr, "generated set has no variants output\n");
    goto cleanup;
  }
  items = calloc(columns * rows, sizeof(*items));
  if (items == NULL) goto cleanup;
  for (item_index = 0; item_index < columns * rows; item_index += 1) {
    bb_artifact_value artifact;
    if (bb_program_output_artifact(program, 2, item_index, &artifact) != BB_STATUS_OK) {
      fprintf(stderr, "could not get generated item %lu\n", (unsigned long)item_index);
      goto cleanup;
    }
    items[item_index] = (bb_sdl_gpu_item){
      artifact.image,
      (float)((item_index % columns) * tile_size),
      (float)((item_index / columns) * tile_size),
      tile_size,
      tile_size,
    };
  }

  if (!SDL_Init(SDL_INIT_VIDEO)) {
    fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
    goto cleanup;
  }
  {
    int driver_index;
    fprintf(stderr, "available SDL_GPU drivers:");
    for (driver_index = 0; driver_index < SDL_GetNumGPUDrivers(); driver_index += 1)
      fprintf(stderr, " %s", SDL_GetGPUDriver(driver_index));
    fputc('\n', stderr);
  }
#ifdef SDL_GPU_SHADERFORMAT_WGSL
  shader_formats |= SDL_GPU_SHADERFORMAT_WGSL;
#endif
  device = SDL_CreateGPUDevice(shader_formats, true, NULL);
  if (device == NULL) {
    fprintf(stderr, "SDL_CreateGPUDevice failed: %s\n", SDL_GetError());
    goto cleanup;
  }
  fprintf(stderr, "SDL_GPU driver: %s\n", SDL_GetGPUDeviceDriver(device));
  if (bb_sdl_gpu_renderer_create(device, &renderer) != BB_STATUS_OK ||
      bb_sdl_gpu_batch_create(
        renderer,
        bb_program_image_graph(program),
        items,
        columns * rows,
        NULL,
        &batch
      ) != BB_STATUS_OK) {
    fprintf(stderr, "could not create GPU batch: %s\n", SDL_GetError());
    goto cleanup;
  }
  {
    SDL_GPUTextureCreateInfo info = {
      .type = SDL_GPU_TEXTURETYPE_2D,
      .format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
      .usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET,
      .width = atlas_width,
      .height = atlas_height,
      .layer_count_or_depth = 1,
      .num_levels = 1,
      .sample_count = SDL_GPU_SAMPLECOUNT_1,
    };
    texture = SDL_CreateGPUTexture(device, &info);
  }
  {
    SDL_GPUTransferBufferCreateInfo info = {
      .usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD,
      .size = atlas_width * atlas_height * 4,
    };
    download = SDL_CreateGPUTransferBuffer(device, &info);
  }
  if (texture == NULL || download == NULL) {
    fprintf(stderr, "could not create readback resources: %s\n", SDL_GetError());
    goto cleanup;
  }
  commands = SDL_AcquireGPUCommandBuffer(device);
  if (commands == NULL) {
    fprintf(stderr, "could not acquire command buffer: %s\n", SDL_GetError());
    goto cleanup;
  }
  {
    const bb_sdl_gpu_target target = {
      texture,
      SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
      atlas_width,
      atlas_height,
      {0, 0, 0, 0},
      SDL_GPU_LOADOP_CLEAR,
    };
    if (bb_sdl_gpu_batch_encode(batch, commands, &target) != BB_STATUS_OK) {
      fprintf(stderr, "could not encode batch: %s\n", SDL_GetError());
      SDL_CancelGPUCommandBuffer(commands);
      commands = NULL;
      goto cleanup;
    }
  }
  fence = SDL_SubmitGPUCommandBufferAndAcquireFence(commands);
  commands = NULL;
  if (fence == NULL || !SDL_WaitForGPUFences(device, true, &fence, 1)) {
    fprintf(stderr, "GPU render submission failed: %s\n", SDL_GetError());
    goto cleanup;
  }
  SDL_ReleaseGPUFence(device, fence);
  fence = NULL;
  commands = SDL_AcquireGPUCommandBuffer(device);
  if (commands == NULL) {
    fprintf(stderr, "could not acquire readback command buffer: %s\n", SDL_GetError());
    goto cleanup;
  }
  {
    SDL_GPUCopyPass *copy = SDL_BeginGPUCopyPass(commands);
    const SDL_GPUTextureRegion source_region = {
      .texture = texture,
      .mip_level = 0,
#if defined(BB_SDL_GPU_WEBGPU_PR_READBACK_WORKAROUND)
      /* PR #16020 passes layer, rather than depth, to WebGPU's copy extent. */
      .layer = 1,
#else
      .layer = 0,
#endif
      .x = 0,
      .y = 0,
      .z = 0,
      .w = atlas_width,
      .h = atlas_height,
      .d = 1,
    };
    const SDL_GPUTextureTransferInfo destination = {
      download, 0, atlas_width, atlas_height,
    };
    if (copy == NULL) {
      fprintf(stderr, "could not begin readback: %s\n", SDL_GetError());
      SDL_CancelGPUCommandBuffer(commands);
      commands = NULL;
      goto cleanup;
    }
    SDL_DownloadFromGPUTexture(copy, &source_region, &destination);
    SDL_EndGPUCopyPass(copy);
  }
  fence = SDL_SubmitGPUCommandBufferAndAcquireFence(commands);
  commands = NULL;
  if (fence == NULL || !SDL_WaitForGPUFences(device, true, &fence, 1)) {
    fprintf(stderr, "GPU submission failed: %s\n", SDL_GetError());
    goto cleanup;
  }
  pixels = SDL_MapGPUTransferBuffer(device, download, false);
  if (pixels == NULL) {
    fprintf(stderr, "could not map readback: %s\n", SDL_GetError());
    goto cleanup;
  }
  for (item_index = 0; item_index < columns * rows; item_index += 1) {
    bb_surface *surface = NULL;
    bb_const_image_view expected;
    uint32_t y;
    if (bb_program_render_output(program, 2, item_index, &surface) != BB_STATUS_OK ||
        bb_surface_get_const_view(surface, &expected) != BB_STATUS_OK) {
      bb_surface_destroy(surface);
      fprintf(stderr, "CPU render failed for item %lu\n", (unsigned long)item_index);
      goto cleanup;
    }
    for (y = 0; y < tile_size; y += 1) {
      uint32_t x;
      for (x = 0; x < tile_size; x += 1) {
        size_t channel;
        const size_t gpu_offset =
          (((item_index / columns) * tile_size + y) * atlas_width +
           (item_index % columns) * tile_size + x) *
          4;
        const size_t cpu_offset = (size_t)y * expected.desc.row_pitch + (size_t)x * 4;
        for (channel = 0; channel < 4; channel += 1) {
          const unsigned left = pixels[gpu_offset + channel];
          const unsigned right = expected.data[cpu_offset + channel];
          const unsigned error = left > right ? left - right : right - left;
          if (error > max_error) max_error = error;
          if (error > 1) {
            if (mismatch_examples < 8) {
              fprintf(
                stderr,
                "mismatch item=%lu pixel=(%u,%u) channel=%lu gpu=%u cpu=%u\n",
                (unsigned long)item_index,
                x,
                y,
                (unsigned long)channel,
                left,
                right
              );
              mismatch_examples += 1;
            }
            mismatch_count += 1;
          }
        }
      }
    }
    bb_surface_destroy(surface);
  }
  SDL_UnmapGPUTransferBuffer(device, download);
  pixels = NULL;
  printf(
    "rendered %u generated variants in one draw; max channel error=%u; errors over tolerance=%lu\n",
    columns * rows,
    max_error,
    (unsigned long)mismatch_count
  );
  result = mismatch_count == 0 ? EXIT_SUCCESS : EXIT_FAILURE;

cleanup:
  if (pixels != NULL) SDL_UnmapGPUTransferBuffer(device, download);
  if (commands != NULL) SDL_CancelGPUCommandBuffer(commands);
  if (fence != NULL) SDL_ReleaseGPUFence(device, fence);
  if (download != NULL) SDL_ReleaseGPUTransferBuffer(device, download);
  if (texture != NULL) SDL_ReleaseGPUTexture(device, texture);
  bb_sdl_gpu_batch_destroy(batch);
  bb_sdl_gpu_renderer_destroy(renderer);
  if (device != NULL) SDL_DestroyGPUDevice(device);
  SDL_Quit();
  free(items);
  bb_program_destroy(program);
  bb_syntax_tree_destroy(syntax);
  bb_context_destroy(context);
  free(source);
  return result;
}
