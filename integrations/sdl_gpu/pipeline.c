#include "binblock_sdl_gpu_internal.h"

#include <SDL3/SDL.h>

#include <stddef.h>
#include <string.h>

#include "binblock_frag_spirv.h"
#include "binblock_frag_wgsl.h"
#include "binblock_vert_spirv.h"
#include "binblock_vert_wgsl.h"

static SDL_GPUShader *bb_sdl_gpu_create_shader(
  bb_sdl_gpu_renderer *renderer,
  SDL_GPUShaderStage stage
) {
  const SDL_GPUShaderFormat available = SDL_GetGPUShaderFormats(renderer->device);
  const char *driver = SDL_GetGPUDeviceDriver(renderer->device);
  SDL_GPUShaderCreateInfo info;
  memset(&info, 0, sizeof(info));
  info.entrypoint = "main";
  info.stage = stage;
  if (stage == SDL_GPU_SHADERSTAGE_VERTEX) {
    info.num_storage_buffers = 1;
    info.num_uniform_buffers = 1;
  } else {
    info.num_storage_buffers = 2;
  }
#ifdef SDL_GPU_SHADERFORMAT_WGSL
  if ((available & SDL_GPU_SHADERFORMAT_WGSL) != 0 && driver != NULL && strcmp(driver, "webgpu") == 0) {
    info.format = SDL_GPU_SHADERFORMAT_WGSL;
    if (stage == SDL_GPU_SHADERSTAGE_VERTEX) {
      info.code = binblock_sdl_gpu_vert_wgsl;
      info.code_size = binblock_sdl_gpu_vert_wgsl_size;
    } else {
      info.code = binblock_sdl_gpu_frag_wgsl;
      info.code_size = binblock_sdl_gpu_frag_wgsl_size;
    }
    return SDL_CreateGPUShader(renderer->device, &info);
  }
#else
  (void)driver;
#endif
  if ((available & SDL_GPU_SHADERFORMAT_SPIRV) == 0) {
    SDL_SetError("binblock SDL_GPU requires WGSL on WebGPU or SPIR-V on this build");
    return NULL;
  }
  info.format = SDL_GPU_SHADERFORMAT_SPIRV;
  if (stage == SDL_GPU_SHADERSTAGE_VERTEX) {
    info.code = binblock_sdl_gpu_vert_spirv;
    info.code_size = binblock_sdl_gpu_vert_spirv_size;
  } else {
    info.code = binblock_sdl_gpu_frag_spirv;
    info.code_size = binblock_sdl_gpu_frag_spirv_size;
  }
  return SDL_CreateGPUShader(renderer->device, &info);
}

bb_status bb_sdl_gpu_ensure_pipeline(
  bb_sdl_gpu_renderer *renderer,
  SDL_GPUTextureFormat format
) {
  SDL_GPUShader *vertex;
  SDL_GPUShader *fragment;
  SDL_GPUColorTargetDescription color;
  SDL_GPUGraphicsPipelineCreateInfo info;
  if (renderer->pipeline != NULL && renderer->pipeline_format == format) return BB_STATUS_OK;
  if (renderer->pipeline != NULL) {
    SDL_ReleaseGPUGraphicsPipeline(renderer->device, renderer->pipeline);
    renderer->pipeline = NULL;
  }
  vertex = bb_sdl_gpu_create_shader(renderer, SDL_GPU_SHADERSTAGE_VERTEX);
  if (vertex == NULL) return BB_STATUS_INTERNAL_ERROR;
  fragment = bb_sdl_gpu_create_shader(renderer, SDL_GPU_SHADERSTAGE_FRAGMENT);
  if (fragment == NULL) {
    SDL_ReleaseGPUShader(renderer->device, vertex);
    return BB_STATUS_INTERNAL_ERROR;
  }
  memset(&color, 0, sizeof(color));
  color.format = format;
  memset(&info, 0, sizeof(info));
  info.vertex_shader = vertex;
  info.fragment_shader = fragment;
  info.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
  info.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;
  info.target_info.color_target_descriptions = &color;
  info.target_info.num_color_targets = 1;
  renderer->pipeline = SDL_CreateGPUGraphicsPipeline(renderer->device, &info);
  SDL_ReleaseGPUShader(renderer->device, fragment);
  SDL_ReleaseGPUShader(renderer->device, vertex);
  if (renderer->pipeline == NULL) return BB_STATUS_INTERNAL_ERROR;
  renderer->pipeline_format = format;
  return BB_STATUS_OK;
}
