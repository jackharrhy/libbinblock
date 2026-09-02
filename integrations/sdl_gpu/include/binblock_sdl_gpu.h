#ifndef BINBLOCK_SDL_GPU_H
#define BINBLOCK_SDL_GPU_H

#include <SDL3/SDL_gpu.h>
#include <binblock/graph.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct bb_sdl_gpu_renderer bb_sdl_gpu_renderer;
typedef struct bb_sdl_gpu_batch bb_sdl_gpu_batch;

typedef struct bb_sdl_gpu_item {
  bb_image_node root;
  float x;
  float y;
  float width;
  float height;
} bb_sdl_gpu_item;

typedef struct bb_sdl_gpu_unsupported {
  size_t item_index;
  bb_image_node node;
  bb_image_node_kind kind;
} bb_sdl_gpu_unsupported;

typedef struct bb_sdl_gpu_target {
  SDL_GPUTexture *texture;
  SDL_GPUTextureFormat format;
  uint32_t width;
  uint32_t height;
  SDL_FColor clear_color;
  SDL_GPULoadOp load_op;
} bb_sdl_gpu_target;

/* The caller owns the device and must keep it alive until the renderer and all
 * of its batches have been destroyed. */
bb_status bb_sdl_gpu_renderer_create(
  SDL_GPUDevice *device,
  bb_sdl_gpu_renderer **out_renderer
);
void bb_sdl_gpu_renderer_destroy(bb_sdl_gpu_renderer *renderer);

/* Lowers and copies the supported graph values. The graph does not have to
 * outlive the returned batch. Batches must be destroyed before their renderer. */
bb_status bb_sdl_gpu_batch_create(
  bb_sdl_gpu_renderer *renderer,
  const bb_image_graph *graph,
  const bb_sdl_gpu_item *items,
  size_t item_count,
  bb_sdl_gpu_unsupported *out_unsupported,
  bb_sdl_gpu_batch **out_batch
);
void bb_sdl_gpu_batch_destroy(bb_sdl_gpu_batch *batch);

/* Records uploads and one instanced render pass. The caller owns command
 * submission, presentation, and device-loss recovery. The first command
 * buffer passed for a batch must be submitted; discard the batch too if that
 * submission is cancelled or fails. */
bb_status bb_sdl_gpu_batch_encode(
  bb_sdl_gpu_batch *batch,
  SDL_GPUCommandBuffer *commands,
  const bb_sdl_gpu_target *target
);

size_t bb_sdl_gpu_batch_item_count(const bb_sdl_gpu_batch *batch);
size_t bb_sdl_gpu_batch_gradient_stop_count(const bb_sdl_gpu_batch *batch);

#ifdef __cplusplus
}
#endif

#endif
