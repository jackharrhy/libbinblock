#include "binblock_sdl_gpu_internal.h"

#include <SDL3/SDL.h>

#include <limits.h>
#include <stdlib.h>
#include <string.h>

_Static_assert(sizeof(bb_sdl_gpu_vec4) == 16, "GPU vec4 layout");
_Static_assert(sizeof(bb_sdl_gpu_uvec4) == 16, "GPU uvec4 layout");
_Static_assert(sizeof(bb_sdl_gpu_brush) == 80, "GPU brush layout");
_Static_assert(sizeof(bb_sdl_gpu_packed_item) == 208, "GPU item layout");
_Static_assert(sizeof(bb_sdl_gpu_packed_stop) == 32, "GPU stop layout");

static void bb_sdl_gpu_release_batch_buffers(bb_sdl_gpu_batch *batch) {
  if (batch->transfer_buffer != NULL)
    SDL_ReleaseGPUTransferBuffer(batch->renderer->device, batch->transfer_buffer);
  if (batch->stop_buffer != NULL)
    SDL_ReleaseGPUBuffer(batch->renderer->device, batch->stop_buffer);
  if (batch->item_buffer != NULL)
    SDL_ReleaseGPUBuffer(batch->renderer->device, batch->item_buffer);
  batch->transfer_buffer = NULL;
  batch->stop_buffer = NULL;
  batch->item_buffer = NULL;
}

static bb_status bb_sdl_gpu_create_batch_buffers(bb_sdl_gpu_batch *batch) {
  SDL_GPUBufferCreateInfo buffer_info;
  SDL_GPUTransferBufferCreateInfo transfer_info;
  bb_sdl_gpu_packed_stop dummy_stop;
  void *mapped;
  uint64_t item_bytes = batch->lowered.item_count * sizeof(*batch->lowered.items);
  uint64_t stop_bytes = batch->lowered.stop_count * sizeof(*batch->lowered.stops);
  uint64_t stop_offset;
  uint64_t transfer_bytes;
  if (stop_bytes == 0) stop_bytes = sizeof(dummy_stop);
  stop_offset = (item_bytes + 15u) & ~UINT64_C(15);
  transfer_bytes = stop_offset + stop_bytes;
  if (item_bytes > UINT32_MAX || stop_bytes > UINT32_MAX ||
      stop_offset > UINT32_MAX || transfer_bytes > UINT32_MAX)
    return BB_STATUS_LIMIT_EXCEEDED;
  batch->item_bytes = (uint32_t)item_bytes;
  batch->stop_bytes = (uint32_t)stop_bytes;
  batch->stop_transfer_offset = (uint32_t)stop_offset;
  memset(&buffer_info, 0, sizeof(buffer_info));
  buffer_info.usage = SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ;
  buffer_info.size = batch->item_bytes;
  batch->item_buffer = SDL_CreateGPUBuffer(batch->renderer->device, &buffer_info);
  if (batch->item_buffer == NULL) goto fail;
  buffer_info.size = batch->stop_bytes;
  batch->stop_buffer = SDL_CreateGPUBuffer(batch->renderer->device, &buffer_info);
  if (batch->stop_buffer == NULL) goto fail;
  memset(&transfer_info, 0, sizeof(transfer_info));
  transfer_info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
  transfer_info.size = (uint32_t)transfer_bytes;
  batch->transfer_buffer = SDL_CreateGPUTransferBuffer(batch->renderer->device, &transfer_info);
  if (batch->transfer_buffer == NULL) goto fail;
  mapped = SDL_MapGPUTransferBuffer(batch->renderer->device, batch->transfer_buffer, false);
  if (mapped == NULL) goto fail;
  memcpy(mapped, batch->lowered.items, batch->item_bytes);
  if (batch->lowered.stop_count != 0)
    memcpy(
      (uint8_t *)mapped + batch->stop_transfer_offset,
      batch->lowered.stops,
      batch->stop_bytes
    );
  else {
    memset(&dummy_stop, 0, sizeof(dummy_stop));
    memcpy((uint8_t *)mapped + batch->stop_transfer_offset, &dummy_stop, sizeof(dummy_stop));
  }
  SDL_UnmapGPUTransferBuffer(batch->renderer->device, batch->transfer_buffer);
  return BB_STATUS_OK;
fail:
  bb_sdl_gpu_release_batch_buffers(batch);
  return BB_STATUS_INTERNAL_ERROR;
}

static bb_status bb_sdl_gpu_upload_batch(
  bb_sdl_gpu_batch *batch,
  SDL_GPUCommandBuffer *commands
) {
  SDL_GPUCopyPass *copy;
  SDL_GPUTransferBufferLocation source;
  SDL_GPUBufferRegion destination;
  if (batch->uploaded) return BB_STATUS_OK;
  if (batch->transfer_buffer == NULL) {
    bb_status status = bb_sdl_gpu_create_batch_buffers(batch);
    if (status != BB_STATUS_OK) return status;
  }
  copy = SDL_BeginGPUCopyPass(commands);
  if (copy == NULL) return BB_STATUS_INTERNAL_ERROR;
  source.transfer_buffer = batch->transfer_buffer;
  source.offset = 0;
  destination.buffer = batch->item_buffer;
  destination.offset = 0;
  destination.size = batch->item_bytes;
  SDL_UploadToGPUBuffer(copy, &source, &destination, false);
  source.offset = batch->stop_transfer_offset;
  destination.buffer = batch->stop_buffer;
  destination.size = batch->stop_bytes;
  SDL_UploadToGPUBuffer(copy, &source, &destination, false);
  SDL_EndGPUCopyPass(copy);
  SDL_ReleaseGPUTransferBuffer(batch->renderer->device, batch->transfer_buffer);
  batch->transfer_buffer = NULL;
  batch->uploaded = 1;
  return BB_STATUS_OK;
}

bb_status bb_sdl_gpu_renderer_create(
  SDL_GPUDevice *device,
  bb_sdl_gpu_renderer **out_renderer
) {
  bb_sdl_gpu_renderer *renderer;
  SDL_GPUShaderFormat formats;
  if (out_renderer == NULL) return BB_STATUS_INVALID_ARGUMENT;
  *out_renderer = NULL;
  if (device == NULL) return BB_STATUS_INVALID_ARGUMENT;
  formats = SDL_GetGPUShaderFormats(device);
#ifdef SDL_GPU_SHADERFORMAT_WGSL
  if ((formats & (SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_WGSL)) == 0)
#else
  if ((formats & SDL_GPU_SHADERFORMAT_SPIRV) == 0)
#endif
    return BB_STATUS_UNSUPPORTED;
  renderer = calloc(1, sizeof(*renderer));
  if (renderer == NULL) return BB_STATUS_OUT_OF_MEMORY;
  renderer->device = device;
  *out_renderer = renderer;
  return BB_STATUS_OK;
}

void bb_sdl_gpu_renderer_destroy(bb_sdl_gpu_renderer *renderer) {
  if (renderer == NULL) return;
  if (renderer->pipeline != NULL)
    SDL_ReleaseGPUGraphicsPipeline(renderer->device, renderer->pipeline);
  free(renderer);
}

bb_status bb_sdl_gpu_batch_create(
  bb_sdl_gpu_renderer *renderer,
  const bb_image_graph *graph,
  const bb_sdl_gpu_item *items,
  size_t item_count,
  bb_sdl_gpu_unsupported *out_unsupported,
  bb_sdl_gpu_batch **out_batch
) {
  bb_sdl_gpu_batch *batch;
  bb_status status;
  if (out_batch == NULL) return BB_STATUS_INVALID_ARGUMENT;
  *out_batch = NULL;
  if (renderer == NULL) return BB_STATUS_INVALID_ARGUMENT;
  batch = calloc(1, sizeof(*batch));
  if (batch == NULL) return BB_STATUS_OUT_OF_MEMORY;
  batch->renderer = renderer;
  status = bb_sdl_gpu_lower(graph, items, item_count, out_unsupported, &batch->lowered);
  if (status != BB_STATUS_OK) {
    free(batch);
    return status;
  }
  *out_batch = batch;
  return BB_STATUS_OK;
}

void bb_sdl_gpu_batch_destroy(bb_sdl_gpu_batch *batch) {
  if (batch == NULL) return;
  bb_sdl_gpu_release_batch_buffers(batch);
  bb_sdl_gpu_lowered_destroy(&batch->lowered);
  free(batch);
}

bb_status bb_sdl_gpu_batch_encode(
  bb_sdl_gpu_batch *batch,
  SDL_GPUCommandBuffer *commands,
  const bb_sdl_gpu_target *target
) {
  bb_sdl_gpu_vec4 target_uniform;
  SDL_GPUColorTargetInfo color;
  SDL_GPURenderPass *pass;
  SDL_GPUBuffer *fragment_buffers[2];
  bb_status status;
  if (batch == NULL || commands == NULL || target == NULL || target->texture == NULL ||
      target->width == 0 || target->height == 0) return BB_STATUS_INVALID_ARGUMENT;
  status = bb_sdl_gpu_upload_batch(batch, commands);
  if (status != BB_STATUS_OK) return status;
  status = bb_sdl_gpu_ensure_pipeline(batch->renderer, target->format);
  if (status != BB_STATUS_OK) return status;
  target_uniform = (bb_sdl_gpu_vec4){(float)target->width, (float)target->height, 0.0f, 0.0f};
  SDL_PushGPUVertexUniformData(commands, 0, &target_uniform, sizeof(target_uniform));
  memset(&color, 0, sizeof(color));
  color.texture = target->texture;
  color.clear_color = target->clear_color;
  color.load_op = target->load_op;
  color.store_op = SDL_GPU_STOREOP_STORE;
  pass = SDL_BeginGPURenderPass(commands, &color, 1, NULL);
  if (pass == NULL) return BB_STATUS_INTERNAL_ERROR;
  SDL_BindGPUGraphicsPipeline(pass, batch->renderer->pipeline);
  SDL_BindGPUVertexStorageBuffers(pass, 0, &batch->item_buffer, 1);
  fragment_buffers[0] = batch->item_buffer;
  fragment_buffers[1] = batch->stop_buffer;
  SDL_BindGPUFragmentStorageBuffers(pass, 0, fragment_buffers, 2);
  SDL_DrawGPUPrimitives(pass, 6, (uint32_t)batch->lowered.item_count, 0, 0);
  SDL_EndGPURenderPass(pass);
  return BB_STATUS_OK;
}

size_t bb_sdl_gpu_batch_item_count(const bb_sdl_gpu_batch *batch) {
  return batch == NULL ? 0 : batch->lowered.item_count;
}

size_t bb_sdl_gpu_batch_gradient_stop_count(const bb_sdl_gpu_batch *batch) {
  return batch == NULL ? 0 : batch->lowered.stop_count;
}
