#include <binblock/graph.h>

#include <stddef.h>
#include <stdint.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  bb_context_desc desc;
  bb_context *context = NULL;
  bb_image_graph *graph = NULL;
  bb_image_node nodes[64];
  size_t node_count = 0;
  size_t offset = 0;
  bb_surface *surface = NULL;
  bb_context_desc_init(&desc);
  desc.limits.max_graph_nodes = 64;
  desc.limits.max_graph_depth = 64;
  if (bb_context_create(&desc, &context) != BB_STATUS_OK ||
      bb_image_graph_create(context, &graph) != BB_STATUS_OK)
    goto cleanup;
  while (offset + 4 <= size && node_count < 64) {
    const uint8_t opcode = data[offset++];
    const uint32_t width = (data[offset++] % 16) + 1;
    const uint32_t height = (data[offset++] % 16) + 1;
    const uint8_t choice = data[offset++];
    bb_image_node node = BB_IMAGE_NODE_NONE;
    bb_status status;
    if (node_count == 0 || opcode % 5 == 0) {
      const bb_rgba8 color = {opcode, choice, (uint8_t)(opcode ^ choice), 255};
      status = bb_image_graph_add_fill(graph, width, height, color, &node);
    } else {
      const bb_image_node source = nodes[choice % node_count];
      if (opcode % 5 == 1)
        status = bb_image_graph_add_crop(graph, source, (int8_t)opcode, (int8_t)choice, width, height, &node);
      else if (opcode % 5 == 2)
        status = bb_image_graph_add_canvas(graph, source, width, height, (int8_t)opcode, (int8_t)choice, &node);
      else if (opcode % 5 == 3)
        status = bb_image_graph_add_rotate(graph, source, (int8_t)choice, &node);
      else
        status = bb_image_graph_add_resize(graph, source, width, height, &node);
    }
    if (status == BB_STATUS_OK) nodes[node_count++] = node;
  }
  if (node_count != 0 && bb_image_graph_seal(graph) == BB_STATUS_OK)
    (void)bb_image_graph_render_raster(context, graph, nodes[node_count - 1], &surface);

cleanup:
  bb_surface_destroy(surface);
  bb_image_graph_destroy(graph);
  bb_context_destroy(context);
  return 0;
}
