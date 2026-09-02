#ifndef BINBLOCK_SDL_GPU_INTERNAL_H
#define BINBLOCK_SDL_GPU_INTERNAL_H

#include "binblock_sdl_gpu.h"

#include <stddef.h>
#include <stdint.h>

typedef struct bb_sdl_gpu_vec4 {
  float x;
  float y;
  float z;
  float w;
} bb_sdl_gpu_vec4;

typedef struct bb_sdl_gpu_uvec4 {
  uint32_t x;
  uint32_t y;
  uint32_t z;
  uint32_t w;
} bb_sdl_gpu_uvec4;

typedef enum bb_sdl_gpu_brush_kind {
  BB_SDL_GPU_BRUSH_NONE = 0,
  BB_SDL_GPU_BRUSH_FILL = 1,
  BB_SDL_GPU_BRUSH_PRESET_GRADIENT = 2,
  BB_SDL_GPU_BRUSH_LINEAR_GRADIENT = 3,
  BB_SDL_GPU_BRUSH_ELLIPTICAL_GRADIENT = 4,
  BB_SDL_GPU_BRUSH_ALPHA_FIELD = 5
} bb_sdl_gpu_brush_kind;

typedef struct bb_sdl_gpu_brush {
  bb_sdl_gpu_uvec4 meta;
  bb_sdl_gpu_vec4 color;
  bb_sdl_gpu_vec4 parameters0;
  bb_sdl_gpu_vec4 parameters1;
  bb_sdl_gpu_vec4 size_opacity;
} bb_sdl_gpu_brush;

typedef struct bb_sdl_gpu_packed_item {
  bb_sdl_gpu_vec4 target;
  bb_sdl_gpu_vec4 source;
  bb_sdl_gpu_vec4 composite;
  bb_sdl_gpu_brush base;
  bb_sdl_gpu_brush overlay;
} bb_sdl_gpu_packed_item;

typedef struct bb_sdl_gpu_packed_stop {
  bb_sdl_gpu_vec4 color;
  bb_sdl_gpu_vec4 parameters;
} bb_sdl_gpu_packed_stop;

typedef struct bb_sdl_gpu_lowered {
  bb_sdl_gpu_packed_item *items;
  size_t item_count;
  bb_sdl_gpu_packed_stop *stops;
  size_t stop_count;
  size_t stop_capacity;
} bb_sdl_gpu_lowered;

struct bb_sdl_gpu_renderer {
  SDL_GPUDevice *device;
  SDL_GPUGraphicsPipeline *pipeline;
  SDL_GPUTextureFormat pipeline_format;
};

struct bb_sdl_gpu_batch {
  bb_sdl_gpu_renderer *renderer;
  bb_sdl_gpu_lowered lowered;
  SDL_GPUBuffer *item_buffer;
  SDL_GPUBuffer *stop_buffer;
  SDL_GPUTransferBuffer *transfer_buffer;
  uint32_t item_bytes;
  uint32_t stop_bytes;
  uint32_t stop_transfer_offset;
  uint32_t uploaded;
};

bb_status bb_sdl_gpu_lower(
  const bb_image_graph *graph,
  const bb_sdl_gpu_item *items,
  size_t item_count,
  bb_sdl_gpu_unsupported *out_unsupported,
  bb_sdl_gpu_lowered *out_lowered
);
void bb_sdl_gpu_lowered_destroy(bb_sdl_gpu_lowered *lowered);

bb_status bb_sdl_gpu_ensure_pipeline(
  bb_sdl_gpu_renderer *renderer,
  SDL_GPUTextureFormat format
);

#endif
