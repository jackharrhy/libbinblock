#include "binblock_sdl_gpu_internal.h"

#include <stdio.h>
#include <stdlib.h>

#define CHECK(expression)                                                              \
  do {                                                                                 \
    if (!(expression)) {                                                               \
      fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #expression); \
      return 0;                                                                        \
    }                                                                                  \
  } while (0)

static int test_generated_set_shape_lowers(void) {
  const bb_gradient_stop stops[] = {
    {0.0, {0, 0, 0, 255}, BB_EASING_LINEAR, 0},
    {1.0, {0, 0, 0, 0}, BB_EASING_LINEAR, 0},
  };
  const bb_linear_gradient_desc gradient = {
    64, 64, 90.0, 0.0, 0, stops, 2, BB_EASING_LINEAR,
  };
  bb_context *context = NULL;
  bb_image_graph *graph = NULL;
  bb_image_node one_pixel;
  bb_image_node fill;
  bb_image_node layer;
  bb_image_node composite;
  bb_sdl_gpu_item items[3];
  bb_sdl_gpu_lowered lowered;
  bb_sdl_gpu_unsupported unsupported;
  CHECK(bb_context_create(NULL, &context) == BB_STATUS_OK);
  CHECK(bb_image_graph_create(context, &graph) == BB_STATUS_OK);
  CHECK(bb_image_graph_add_fill(graph, 1, 1, (bb_rgba8){0, 0, 255, 255}, &one_pixel) == BB_STATUS_OK);
  CHECK(bb_image_graph_add_resize(graph, one_pixel, 64, 64, &fill) == BB_STATUS_OK);
  CHECK(bb_image_graph_add_linear_gradient(graph, &gradient, &layer) == BB_STATUS_OK);
  CHECK(bb_image_graph_add_composite(graph, fill, layer, 0, 0, 0.5, &composite) == BB_STATUS_OK);
  items[0] = (bb_sdl_gpu_item){fill, 0, 0, 64, 64};
  items[1] = (bb_sdl_gpu_item){layer, 64, 0, 64, 64};
  items[2] = (bb_sdl_gpu_item){composite, 128, 0, 64, 64};
  CHECK(bb_sdl_gpu_lower(graph, items, 3, &unsupported, &lowered) == BB_STATUS_OK);
  CHECK(lowered.item_count == 3);
  CHECK(lowered.stop_count == 4);
  CHECK(lowered.items[0].base.meta.x == BB_SDL_GPU_BRUSH_FILL);
  CHECK(lowered.items[0].base.size_opacity.x == 64.0f);
  CHECK(lowered.items[1].base.meta.x == BB_SDL_GPU_BRUSH_LINEAR_GRADIENT);
  CHECK(lowered.items[2].base.meta.x == BB_SDL_GPU_BRUSH_FILL);
  CHECK(lowered.items[2].overlay.meta.x == BB_SDL_GPU_BRUSH_LINEAR_GRADIENT);
  CHECK(lowered.items[2].composite.z == 0.5f);
  bb_sdl_gpu_lowered_destroy(&lowered);
  bb_image_graph_destroy(graph);
  bb_context_destroy(context);
  return 1;
}

static int test_lanczos_resize_reports_the_node(void) {
  const bb_gradient_stop stops[] = {
    {0.0, {255, 255, 255, 255}, BB_EASING_LINEAR, 0},
    {1.0, {255, 255, 255, 0}, BB_EASING_LINEAR, 0},
  };
  const bb_linear_gradient_desc gradient = {
    64, 64, 0.0, 0.0, 0, stops, 2, BB_EASING_LINEAR,
  };
  bb_context *context = NULL;
  bb_image_graph *graph = NULL;
  bb_image_node source;
  bb_image_node resized;
  bb_sdl_gpu_item item;
  bb_sdl_gpu_lowered lowered;
  bb_sdl_gpu_unsupported unsupported;
  CHECK(bb_context_create(NULL, &context) == BB_STATUS_OK);
  CHECK(bb_image_graph_create(context, &graph) == BB_STATUS_OK);
  CHECK(bb_image_graph_add_linear_gradient(graph, &gradient, &source) == BB_STATUS_OK);
  CHECK(bb_image_graph_add_resize(graph, source, 8, 8, &resized) == BB_STATUS_OK);
  item = (bb_sdl_gpu_item){resized, 0, 0, 8, 8};
  CHECK(bb_sdl_gpu_lower(graph, &item, 1, &unsupported, &lowered) == BB_STATUS_UNSUPPORTED);
  CHECK(unsupported.item_index == 0);
  CHECK(unsupported.node == resized);
  CHECK(unsupported.kind == BB_IMAGE_NODE_RESIZE);
  bb_image_graph_destroy(graph);
  bb_context_destroy(context);
  return 1;
}

static int test_assets_remain_a_host_fallback(void) {
  const bb_graph_asset_desc asset = {
    {"sha256:test", sizeof("sha256:test") - 1},
    64,
    64,
  };
  bb_context *context = NULL;
  bb_image_graph *graph = NULL;
  bb_image_node root;
  bb_sdl_gpu_item item;
  bb_sdl_gpu_lowered lowered;
  bb_sdl_gpu_unsupported unsupported;
  CHECK(bb_context_create(NULL, &context) == BB_STATUS_OK);
  CHECK(bb_image_graph_create(context, &graph) == BB_STATUS_OK);
  CHECK(bb_image_graph_add_asset(graph, &asset, &root) == BB_STATUS_OK);
  item = (bb_sdl_gpu_item){root, 0, 0, 64, 64};
  CHECK(bb_sdl_gpu_lower(graph, &item, 1, &unsupported, &lowered) == BB_STATUS_UNSUPPORTED);
  CHECK(unsupported.node == root && unsupported.kind == BB_IMAGE_NODE_ASSET);
  bb_image_graph_destroy(graph);
  bb_context_destroy(context);
  return 1;
}

int main(void) {
  size_t passed = 0;
  passed += test_generated_set_shape_lowers();
  passed += test_lanczos_resize_reports_the_node();
  passed += test_assets_remain_a_host_fallback();
  printf("%lu/3 SDL_GPU lowering tests passed\n", (unsigned long)passed);
  return passed == 3 ? EXIT_SUCCESS : EXIT_FAILURE;
}
