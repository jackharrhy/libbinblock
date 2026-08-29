#include <binblock/graph.h>

#include "checked_math.h"
#include "context_internal.h"
#include "graph_internal.h"

#include <string.h>

static void bb_graph_push_children(
  const bb_graph_node *node,
  uint8_t *needed,
  bb_image_node *stack,
  size_t *stack_count
) {
  bb_image_node children[2] = {BB_IMAGE_NODE_NONE, BB_IMAGE_NODE_NONE};
  size_t child_count = 0;
  size_t index;
  switch (node->kind) {
    case BB_IMAGE_NODE_CROP:
      children[child_count++] = node->data.crop.source;
      break;
    case BB_IMAGE_NODE_CANVAS:
      children[child_count++] = node->data.canvas.source;
      break;
    case BB_IMAGE_NODE_ROTATE:
      children[child_count++] = node->data.rotate.source;
      break;
    case BB_IMAGE_NODE_OPACITY:
      children[child_count++] = node->data.opacity.source;
      break;
    case BB_IMAGE_NODE_COMPOSITE:
      children[child_count++] = node->data.composite.destination;
      children[child_count++] = node->data.composite.source;
      break;
    case BB_IMAGE_NODE_MASK:
      children[child_count++] = node->data.mask.source;
      children[child_count++] = node->data.mask.mask;
      break;
    case BB_IMAGE_NODE_RESIZE:
      children[child_count++] = node->data.resize.source;
      break;
    case BB_IMAGE_NODE_INVERT_ALPHA:
      children[child_count++] = node->data.invert_alpha.source;
      break;
    case BB_IMAGE_NODE_SET_VISIBLE_RGB:
    case BB_IMAGE_NODE_TINT_CHROMA:
      children[child_count++] = node->data.color_transform.source;
      break;
    case BB_IMAGE_NODE_REMAP_TWO_COLOR:
      children[child_count++] = node->data.remap.source;
      break;
    case BB_IMAGE_NODE_SHIFT_RGB:
      children[child_count++] = node->data.shift_rgb.source;
      break;
    default:
      break;
  }
  for (index = 0; index < child_count; index += 1) {
    const bb_image_node child = children[index];
    if (!needed[child - 1]) {
      needed[child - 1] = 1;
      stack[*stack_count] = child;
      *stack_count += 1;
    }
  }
}

static bb_status bb_graph_render_node(
  bb_context *context,
  const bb_graph_node *node,
  bb_surface *const *surfaces,
  bb_graph_asset_decode_fn decode,
  void *decode_user,
  bb_surface **out_surface
) {
  switch (node->kind) {
    case BB_IMAGE_NODE_FILL:
      return bb_raster_fill(
        context,
        node->data.fill.width,
        node->data.fill.height,
        node->data.fill.color,
        out_surface
      );
    case BB_IMAGE_NODE_ASSET: {
      bb_const_image_view image;
      bb_status status;
      if (decode == NULL) return BB_STATUS_UNSUPPORTED;
      memset(&image, 0, sizeof(image));
      status = decode(decode_user, node->data.asset.desc.content_id, &image);
      if (status != BB_STATUS_OK) return status;
      if (image.desc.width != node->data.asset.desc.width || image.desc.height != node->data.asset.desc.height)
        return BB_STATUS_INVALID_ARGUMENT;
      return bb_surface_create_from_rgba8(context, &image, out_surface);
    }
    case BB_IMAGE_NODE_ALPHA_FIELD:
      return bb_raster_alpha_field(context, &node->data.alpha_field.desc, out_surface);
    case BB_IMAGE_NODE_PRESET_GRADIENT:
      return bb_raster_preset_gradient(
        context,
        node->data.preset.width,
        node->data.preset.height,
        node->data.preset.preset,
        node->data.preset.turns,
        node->data.preset.color,
        out_surface
      );
    case BB_IMAGE_NODE_LINEAR_GRADIENT:
      return bb_raster_linear_gradient(context, &node->data.linear.desc, out_surface);
    case BB_IMAGE_NODE_ELLIPTICAL_GRADIENT:
      return bb_raster_elliptical_gradient(context, &node->data.ellipse.desc, out_surface);
    case BB_IMAGE_NODE_CROP:
      return bb_raster_crop(
        context,
        surfaces[node->data.crop.source],
        node->data.crop.x,
        node->data.crop.y,
        node->data.crop.width,
        node->data.crop.height,
        out_surface
      );
    case BB_IMAGE_NODE_CANVAS:
      return bb_raster_canvas(
        context,
        surfaces[node->data.canvas.source],
        node->data.canvas.width,
        node->data.canvas.height,
        node->data.canvas.x,
        node->data.canvas.y,
        out_surface
      );
    case BB_IMAGE_NODE_ROTATE:
      return bb_raster_rotate_quarter_turns(
        context,
        surfaces[node->data.rotate.source],
        node->data.rotate.turns,
        out_surface
      );
    case BB_IMAGE_NODE_OPACITY:
      return bb_raster_opacity(
        context,
        surfaces[node->data.opacity.source],
        node->data.opacity.opacity,
        out_surface
      );
    case BB_IMAGE_NODE_COMPOSITE:
      return bb_raster_source_over(
        context,
        surfaces[node->data.composite.destination],
        surfaces[node->data.composite.source],
        node->data.composite.offset_x,
        node->data.composite.offset_y,
        node->data.composite.opacity,
        out_surface
      );
    case BB_IMAGE_NODE_MASK:
      return bb_raster_apply_mask(
        context,
        surfaces[node->data.mask.source],
        surfaces[node->data.mask.mask],
        node->data.mask.mode,
        out_surface
      );
    case BB_IMAGE_NODE_RESIZE:
      return bb_raster_resize_lanczos3(
        context,
        surfaces[node->data.resize.source],
        node->data.resize.width,
        node->data.resize.height,
        out_surface
      );
    case BB_IMAGE_NODE_INVERT_ALPHA:
      return bb_raster_invert_alpha(context, surfaces[node->data.invert_alpha.source], out_surface);
    case BB_IMAGE_NODE_SET_VISIBLE_RGB:
      return bb_raster_set_visible_rgb(
        context,
        surfaces[node->data.color_transform.source],
        node->data.color_transform.color,
        out_surface
      );
    case BB_IMAGE_NODE_TINT_CHROMA:
      return bb_raster_tint_chroma(
        context,
        surfaces[node->data.color_transform.source],
        node->data.color_transform.color,
        out_surface
      );
    case BB_IMAGE_NODE_REMAP_TWO_COLOR:
      return bb_raster_remap_two_color(
        context,
        surfaces[node->data.remap.source],
        node->data.remap.source_foreground,
        node->data.remap.source_background,
        node->data.remap.foreground,
        node->data.remap.background,
        out_surface
      );
    case BB_IMAGE_NODE_SHIFT_RGB:
      return bb_raster_shift_rgb(
        context,
        surfaces[node->data.shift_rgb.source],
        node->data.shift_rgb.source_base,
        node->data.shift_rgb.target_base,
        out_surface
      );
    default:
      return BB_STATUS_UNSUPPORTED;
  }
}

void bb_render_options_init(bb_render_options *options) {
  if (options == NULL) return;
  memset(options, 0, sizeof(*options));
  options->struct_size = sizeof(*options);
  options->max_work_units = UINT64_MAX;
}

bb_status bb_image_graph_render_raster_with_options(
  bb_context *context,
  const bb_image_graph *graph,
  bb_image_node root,
  const bb_render_options *options,
  bb_graph_asset_decode_fn decode,
  void *decode_user,
  bb_surface **out_surface
) {
  bb_render_options effective_options;
  uint8_t *needed = NULL;
  bb_image_node *stack = NULL;
  bb_surface **surfaces = NULL;
  size_t stack_bytes;
  size_t surface_bytes;
  size_t stack_count = 0;
  size_t index;
  uint64_t work_units = 0;
  bb_status status;
  if (out_surface == NULL) return BB_STATUS_INVALID_ARGUMENT;
  *out_surface = NULL;
  if (context == NULL || graph == NULL || bb_image_graph_get_node(graph, root) == NULL) {
    return BB_STATUS_INVALID_ARGUMENT;
  }
  bb_render_options_init(&effective_options);
  if (options != NULL) {
    if (options->struct_size != sizeof(*options)) return BB_STATUS_INVALID_ARGUMENT;
    effective_options = *options;
  }
  if (effective_options.should_cancel != NULL && effective_options.should_cancel(effective_options.user))
    return BB_STATUS_CANCELLED;
  if (!bb_size_multiply(graph->node_count, sizeof(*stack), &stack_bytes) ||
      !bb_size_multiply(graph->node_count + 1, sizeof(*surfaces), &surface_bytes)) {
    return BB_STATUS_OVERFLOW;
  }
  status = bb_context_allocate(context, graph->node_count, _Alignof(uint8_t), (void **)&needed);
  if (status != BB_STATUS_OK) return status;
  memset(needed, 0, graph->node_count);
  status = bb_context_allocate(context, stack_bytes, _Alignof(bb_image_node), (void **)&stack);
  if (status != BB_STATUS_OK) goto cleanup;
  needed[root - 1] = 1;
  stack[stack_count++] = root;
  while (stack_count != 0) {
    const bb_image_node current = stack[--stack_count];
    bb_graph_push_children(bb_image_graph_get_node(graph, current), needed, stack, &stack_count);
  }
  for (index = 0; index < graph->node_count; index += 1) {
    uint64_t node_work;
    uint32_t width;
    uint32_t height;
    if (!needed[index]) continue;
    status = bb_image_graph_node_dimensions(graph, (bb_image_node)(index + 1), &width, &height);
    if (status != BB_STATUS_OK) goto cleanup;
    node_work = (uint64_t)width * (uint64_t)height;
    if (UINT64_MAX - work_units < node_work || work_units + node_work > effective_options.max_work_units) {
      status = BB_STATUS_LIMIT_EXCEEDED;
      goto cleanup;
    }
    work_units += node_work;
  }
  status = bb_context_allocate(context, surface_bytes, _Alignof(bb_surface *), (void **)&surfaces);
  if (status != BB_STATUS_OK) goto cleanup;
  memset(surfaces, 0, surface_bytes);
  for (index = 0; index < graph->node_count; index += 1) {
    if (!needed[index]) continue;
    if (effective_options.should_cancel != NULL && effective_options.should_cancel(effective_options.user)) {
      status = BB_STATUS_CANCELLED;
      goto cleanup;
    }
    status = bb_graph_render_node(
      context,
      &graph->nodes[index],
      surfaces,
      decode,
      decode_user,
      &surfaces[index + 1]
    );
    if (status != BB_STATUS_OK) goto cleanup;
  }
  if (effective_options.should_cancel != NULL && effective_options.should_cancel(effective_options.user)) {
    status = BB_STATUS_CANCELLED;
    goto cleanup;
  }
  *out_surface = surfaces[root];
  surfaces[root] = NULL;
  status = BB_STATUS_OK;

cleanup:
  if (surfaces != NULL) {
    for (index = 1; index <= graph->node_count; index += 1) bb_surface_destroy(surfaces[index]);
  }
  bb_context_deallocate(context, surfaces, surface_bytes, _Alignof(bb_surface *));
  bb_context_deallocate(context, stack, stack_bytes, _Alignof(bb_image_node));
  bb_context_deallocate(context, needed, graph->node_count, _Alignof(uint8_t));
  return status;
}

bb_status bb_image_graph_render_raster_with_assets(
  bb_context *context,
  const bb_image_graph *graph,
  bb_image_node root,
  bb_graph_asset_decode_fn decode,
  void *decode_user,
  bb_surface **out_surface
) {
  return bb_image_graph_render_raster_with_options(
    context,
    graph,
    root,
    NULL,
    decode,
    decode_user,
    out_surface
  );
}

bb_status bb_image_graph_render_raster(
  bb_context *context,
  const bb_image_graph *graph,
  bb_image_node root,
  bb_surface **out_surface
) {
  return bb_image_graph_render_raster_with_assets(context, graph, root, NULL, NULL, out_surface);
}
