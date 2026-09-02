#define SDL_MAIN_USE_CALLBACKS 1

#include "binblock_sdl_gpu.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <binblock/program.h>
#include <binblock/syntax.h>

#include <stdlib.h>

#include "generated_set_binscript.h"

enum {
  demo_columns = 16,
  demo_rows = 16,
  demo_item_count = demo_columns * demo_rows,
  demo_window_size = 768,
  demo_tile_size = 44,
  demo_tile_stride = 46,
  demo_margin = 16,
};

typedef struct demo_state {
  bb_context *context;
  bb_syntax_tree *syntax;
  bb_program *program;
  bb_sdl_gpu_renderer *renderer;
  bb_sdl_gpu_batch *batch;
  SDL_Window *window;
  SDL_GPUDevice *device;
  bool rendered;
} demo_state;

static void demo_log_diagnostics(const bb_program *program) {
  size_t index;
  for (index = 0; index < bb_program_diagnostic_count(program); index += 1) {
    bb_diagnostic diagnostic;
    if (bb_program_diagnostic(program, index, &diagnostic) == BB_STATUS_OK) {
      SDL_LogError(
        SDL_LOG_CATEGORY_APPLICATION,
        "%.*s",
        (int)diagnostic.message.length,
        diagnostic.message.data
      );
    }
  }
}

static bool demo_compile(demo_state *state) {
  static const char source_name[] = "generated-set.binscript";
  bb_source_id source_id = BB_SOURCE_ID_NONE;
  if (bb_context_create(NULL, &state->context) != BB_STATUS_OK ||
      bb_context_add_source(
        state->context,
        (bb_string_view){source_name, sizeof(source_name) - 1},
        (bb_bytes){binblock_sdl_gpu_demo_source, binblock_sdl_gpu_demo_source_size},
        &source_id
      ) != BB_STATUS_OK ||
      bb_syntax_parse(state->context, source_id, &state->syntax) != BB_STATUS_OK ||
      bb_program_compile(state->context, state->syntax, &state->program) != BB_STATUS_OK) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "could not compile the generated set");
    return false;
  }
  demo_log_diagnostics(state->program);
  return bb_program_output_count(state->program) >= 3;
}

static bool demo_create_batch(demo_state *state) {
  bb_sdl_gpu_item items[demo_item_count];
  size_t index;
  for (index = 0; index < demo_item_count; index += 1) {
    bb_artifact_value artifact;
    if (bb_program_output_artifact(state->program, 2, index, &artifact) != BB_STATUS_OK) {
      SDL_LogError(
        SDL_LOG_CATEGORY_APPLICATION,
        "could not read generated item %lu",
        (unsigned long)index
      );
      return false;
    }
    items[index] = (bb_sdl_gpu_item){
      .root = artifact.image,
      .x = (float)(demo_margin + (index % demo_columns) * demo_tile_stride),
      .y = (float)(demo_margin + (index / demo_columns) * demo_tile_stride),
      .width = demo_tile_size,
      .height = demo_tile_size,
    };
  }
  return bb_sdl_gpu_batch_create(
           state->renderer,
           bb_program_image_graph(state->program),
           items,
           demo_item_count,
           NULL,
           &state->batch
         ) == BB_STATUS_OK;
}

SDL_AppResult SDL_AppInit(void **appstate, int argc, char **argv) {
  demo_state *state;
  (void)argc;
  (void)argv;
  state = calloc(1, sizeof(*state));
  if (state == NULL) return SDL_APP_FAILURE;
  *appstate = state;
  if (!SDL_Init(SDL_INIT_VIDEO)) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SDL_Init failed: %s", SDL_GetError());
    return SDL_APP_FAILURE;
  }
  if (!demo_compile(state)) return SDL_APP_FAILURE;
  state->window = SDL_CreateWindow(
    "BinScript — SDL3 GPU",
    demo_window_size,
    demo_window_size,
    SDL_WINDOW_RESIZABLE
  );
  state->device = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_WGSL, true, "webgpu");
  if (state->window == NULL || state->device == NULL ||
      !SDL_ClaimWindowForGPUDevice(state->device, state->window) ||
      bb_sdl_gpu_renderer_create(state->device, &state->renderer) != BB_STATUS_OK ||
      !demo_create_batch(state)) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "GPU setup failed: %s", SDL_GetError());
    return SDL_APP_FAILURE;
  }
  SDL_Log(
    "rendering %u BinScript variants with SDL_GPU/%s in one draw",
    demo_item_count,
    SDL_GetGPUDeviceDriver(state->device)
  );
  return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppEvent(void *appstate, SDL_Event *event) {
  (void)appstate;
  if (event->type == SDL_EVENT_QUIT) return SDL_APP_SUCCESS;
  return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppIterate(void *appstate) {
  demo_state *state = appstate;
  SDL_GPUCommandBuffer *commands;
  SDL_GPUTexture *swapchain = NULL;
  uint32_t width = 0;
  uint32_t height = 0;
  bb_sdl_gpu_target target;
  if (state->rendered) return SDL_APP_CONTINUE;
  commands = SDL_AcquireGPUCommandBuffer(state->device);
  if (commands == NULL) {
    SDL_LogError(
      SDL_LOG_CATEGORY_APPLICATION,
      "could not acquire a command buffer: %s",
      SDL_GetError()
    );
    return SDL_APP_FAILURE;
  }
  if (!SDL_WaitAndAcquireGPUSwapchainTexture(
        commands,
        state->window,
        &swapchain,
        &width,
        &height
      )) {
    SDL_SubmitGPUCommandBuffer(commands);
    SDL_LogError(
      SDL_LOG_CATEGORY_APPLICATION,
      "could not acquire the swapchain: %s",
      SDL_GetError()
    );
    return SDL_APP_FAILURE;
  }
  if (swapchain == NULL) {
    SDL_SubmitGPUCommandBuffer(commands);
    return SDL_APP_CONTINUE;
  }
  target = (bb_sdl_gpu_target){
    .texture = swapchain,
    .format = SDL_GetGPUSwapchainTextureFormat(state->device, state->window),
    .width = width,
    .height = height,
    .clear_color = {0.96f, 0.95f, 0.92f, 1.0f},
    .load_op = SDL_GPU_LOADOP_CLEAR,
  };
  if (bb_sdl_gpu_batch_encode(state->batch, commands, &target) != BB_STATUS_OK) {
    SDL_SubmitGPUCommandBuffer(commands);
    SDL_LogError(
      SDL_LOG_CATEGORY_APPLICATION,
      "could not encode the batch: %s",
      SDL_GetError()
    );
    return SDL_APP_FAILURE;
  }
  if (!SDL_SubmitGPUCommandBuffer(commands)) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "could not render the batch: %s", SDL_GetError());
    return SDL_APP_FAILURE;
  }
  state->rendered = true;
  return SDL_APP_CONTINUE;
}

void SDL_AppQuit(void *appstate, SDL_AppResult result) {
  demo_state *state = appstate;
  (void)result;
  if (state == NULL) return;
  bb_sdl_gpu_batch_destroy(state->batch);
  bb_sdl_gpu_renderer_destroy(state->renderer);
  if (state->device != NULL && state->window != NULL)
    SDL_ReleaseWindowFromGPUDevice(state->device, state->window);
  if (state->device != NULL) SDL_DestroyGPUDevice(state->device);
  if (state->window != NULL) SDL_DestroyWindow(state->window);
  bb_program_destroy(state->program);
  bb_syntax_tree_destroy(state->syntax);
  bb_context_destroy(state->context);
  SDL_Quit();
  free(state);
}
