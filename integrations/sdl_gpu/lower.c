#include "binblock_sdl_gpu_internal.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

static bb_sdl_gpu_vec4 bb_sdl_gpu_color(bb_rgba8 color) {
  return (bb_sdl_gpu_vec4){
    color.red / 255.0f,
    color.green / 255.0f,
    color.blue / 255.0f,
    color.alpha / 255.0f,
  };
}

static void bb_sdl_gpu_mark_unsupported(
  bb_sdl_gpu_unsupported *unsupported,
  size_t item_index,
  bb_image_node node,
  bb_image_node_kind kind
) {
  if (unsupported == NULL) return;
  unsupported->item_index = item_index;
  unsupported->node = node;
  unsupported->kind = kind;
}

static bb_status bb_sdl_gpu_append_stops(
  bb_sdl_gpu_lowered *lowered,
  const bb_gradient_stop *stops,
  size_t stop_count,
  uint32_t *out_offset
) {
  size_t required;
  size_t capacity;
  bb_sdl_gpu_packed_stop *values;
  size_t index;
  if (stops == NULL || stop_count < 2 || out_offset == NULL)
    return BB_STATUS_INVALID_ARGUMENT;
  if (lowered->stop_count > UINT32_MAX || stop_count > UINT32_MAX - lowered->stop_count)
    return BB_STATUS_OVERFLOW;
  required = lowered->stop_count + stop_count;
  if (required > lowered->stop_capacity) {
    capacity = lowered->stop_capacity == 0 ? 16 : lowered->stop_capacity;
    while (capacity < required) {
      if (capacity > SIZE_MAX / 2) return BB_STATUS_OVERFLOW;
      capacity *= 2;
    }
    if (capacity > SIZE_MAX / sizeof(*values)) return BB_STATUS_OVERFLOW;
    values = realloc(lowered->stops, capacity * sizeof(*values));
    if (values == NULL) return BB_STATUS_OUT_OF_MEMORY;
    lowered->stops = values;
    lowered->stop_capacity = capacity;
  }
  *out_offset = (uint32_t)lowered->stop_count;
  for (index = 0; index < stop_count; index += 1) {
    lowered->stops[lowered->stop_count++] = (bb_sdl_gpu_packed_stop){
      bb_sdl_gpu_color(stops[index].color),
      {
        (float)stops[index].offset,
        (float)stops[index].easing,
        stops[index].has_easing ? 1.0f : 0.0f,
        0.0f,
      },
    };
  }
  return BB_STATUS_OK;
}

static bb_status bb_sdl_gpu_lower_brush(
  const bb_image_graph *graph,
  bb_image_node node,
  size_t item_index,
  bb_sdl_gpu_lowered *lowered,
  bb_sdl_gpu_unsupported *unsupported,
  bb_sdl_gpu_brush *out_brush
) {
  bb_image_node_info info = {0};
  bb_status status = bb_image_graph_node_info(graph, node, &info);
  uint32_t stop_offset;
  if (status != BB_STATUS_OK) return status;
  memset(out_brush, 0, sizeof(*out_brush));
  out_brush->size_opacity = (bb_sdl_gpu_vec4){(float)info.width, (float)info.height, 1.0f, 0.0f};
  switch (info.kind) {
    case BB_IMAGE_NODE_FILL:
      out_brush->meta.x = BB_SDL_GPU_BRUSH_FILL;
      out_brush->color = bb_sdl_gpu_color(info.options.fill_color);
      return BB_STATUS_OK;
    case BB_IMAGE_NODE_PRESET_GRADIENT:
      out_brush->meta = (bb_sdl_gpu_uvec4){
        BB_SDL_GPU_BRUSH_PRESET_GRADIENT,
        (uint32_t)info.options.preset_gradient.preset,
        (uint32_t)(((info.options.preset_gradient.quarter_turns % 4) + 4) % 4),
        0,
      };
      out_brush->color = bb_sdl_gpu_color(info.options.preset_gradient.color);
      return BB_STATUS_OK;
    case BB_IMAGE_NODE_LINEAR_GRADIENT: {
      const bb_linear_gradient_desc *gradient = &info.options.linear_gradient;
      const double radians = gradient->angle_degrees * (acos(-1.0) / 180.0);
      double direction_x = sin(radians);
      double direction_y = -cos(radians);
      double extent;
      double length;
      if (fabs(direction_x) < 1e-12)
        direction_x = 0.0;
      else if (fabs(fabs(direction_x) - 1.0) < 1e-12)
        direction_x = copysign(1.0, direction_x);
      if (fabs(direction_y) < 1e-12)
        direction_y = 0.0;
      else if (fabs(fabs(direction_y) - 1.0) < 1e-12)
        direction_y = copysign(1.0, direction_y);
      extent = (fabs(direction_x) * fmax(1.0, info.width - 1.0) +
                fabs(direction_y) * fmax(1.0, info.height - 1.0)) /
               2.0;
      if (extent == 0.0) extent = 1.0;
      length = gradient->has_explicit_extent ? gradient->extent : 2.0 * extent;
      status = bb_sdl_gpu_append_stops(lowered, gradient->stops, gradient->stop_count, &stop_offset);
      if (status != BB_STATUS_OK) return status;
      out_brush->meta = (bb_sdl_gpu_uvec4){
        BB_SDL_GPU_BRUSH_LINEAR_GRADIENT,
        stop_offset,
        (uint32_t)gradient->stop_count,
        (uint32_t)gradient->easing,
      };
      out_brush->parameters0 = (bb_sdl_gpu_vec4){
        (float)direction_x,
        (float)direction_y,
        (float)extent,
        (float)length,
      };
      return BB_STATUS_OK;
    }
    case BB_IMAGE_NODE_ELLIPTICAL_GRADIENT: {
      const bb_elliptical_gradient_desc *gradient = &info.options.elliptical_gradient;
      if (gradient->reference_radial_rounding) {
        bb_sdl_gpu_mark_unsupported(unsupported, item_index, node, info.kind);
        return BB_STATUS_UNSUPPORTED;
      }
      status = bb_sdl_gpu_append_stops(lowered, gradient->stops, gradient->stop_count, &stop_offset);
      if (status != BB_STATUS_OK) return status;
      out_brush->meta = (bb_sdl_gpu_uvec4){
        BB_SDL_GPU_BRUSH_ELLIPTICAL_GRADIENT,
        stop_offset,
        (uint32_t)gradient->stop_count,
        (uint32_t)gradient->easing,
      };
      out_brush->parameters0 = (bb_sdl_gpu_vec4){
        (float)gradient->center_x,
        (float)gradient->center_y,
        (float)gradient->radius_x,
        (float)gradient->radius_y,
      };
      out_brush->parameters1 = (bb_sdl_gpu_vec4){
        (float)cos(gradient->rotation_radians),
        (float)sin(gradient->rotation_radians),
        0.0f,
        0.0f,
      };
      return BB_STATUS_OK;
    }
    case BB_IMAGE_NODE_ALPHA_FIELD: {
      const bb_alpha_field_desc *field = &info.options.alpha_field;
      if (field->level_count != 0 || field->reference_radial_rounding) {
        bb_sdl_gpu_mark_unsupported(unsupported, item_index, node, info.kind);
        return BB_STATUS_UNSUPPORTED;
      }
      out_brush->meta = (bb_sdl_gpu_uvec4){
        BB_SDL_GPU_BRUSH_ALPHA_FIELD,
        (uint32_t)field->metric,
        (uint32_t)field->direction,
        (uint32_t)field->easing,
      };
      out_brush->color = bb_sdl_gpu_color(field->color);
      out_brush->parameters0 = (bb_sdl_gpu_vec4){
        (float)field->center_x,
        (float)field->center_y,
        (float)field->radius,
        0.0f,
      };
      return BB_STATUS_OK;
    }
    case BB_IMAGE_NODE_OPACITY:
      status = bb_sdl_gpu_lower_brush(
        graph,
        info.inputs[0],
        item_index,
        lowered,
        unsupported,
        out_brush
      );
      if (status == BB_STATUS_OK) out_brush->size_opacity.z *= (float)info.options.opacity;
      return status;
    case BB_IMAGE_NODE_RESIZE: {
      bb_image_node_info child = {0};
      status = bb_image_graph_node_info(graph, info.inputs[0], &child);
      if (status != BB_STATUS_OK) return status;
      status = bb_sdl_gpu_lower_brush(
        graph,
        info.inputs[0],
        item_index,
        lowered,
        unsupported,
        out_brush
      );
      if (status != BB_STATUS_OK) return status;
      if (out_brush->meta.x != BB_SDL_GPU_BRUSH_FILL &&
          (child.width != info.width || child.height != info.height)) {
        bb_sdl_gpu_mark_unsupported(unsupported, item_index, node, info.kind);
        return BB_STATUS_UNSUPPORTED;
      }
      out_brush->size_opacity.x = (float)info.width;
      out_brush->size_opacity.y = (float)info.height;
      return BB_STATUS_OK;
    }
    default:
      bb_sdl_gpu_mark_unsupported(unsupported, item_index, node, info.kind);
      return BB_STATUS_UNSUPPORTED;
  }
}

static bb_status bb_sdl_gpu_lower_item(
  const bb_image_graph *graph,
  const bb_sdl_gpu_item *item,
  size_t item_index,
  bb_sdl_gpu_lowered *lowered,
  bb_sdl_gpu_unsupported *unsupported,
  bb_sdl_gpu_packed_item *out_item
) {
  bb_image_node_info info = {0};
  bb_status status;
  memset(out_item, 0, sizeof(*out_item));
  status = bb_image_graph_node_info(graph, item->root, &info);
  if (status != BB_STATUS_OK) return status;
  out_item->target = (bb_sdl_gpu_vec4){item->x, item->y, item->width, item->height};
  out_item->source = (bb_sdl_gpu_vec4){(float)info.width, (float)info.height, 0.0f, 0.0f};
  out_item->composite = (bb_sdl_gpu_vec4){0.0f, 0.0f, 1.0f, 1.0f};
  if (info.kind == BB_IMAGE_NODE_COMPOSITE) {
    status = bb_sdl_gpu_lower_brush(
      graph,
      info.inputs[0],
      item_index,
      lowered,
      unsupported,
      &out_item->base
    );
    if (status != BB_STATUS_OK) return status;
    status = bb_sdl_gpu_lower_brush(
      graph,
      info.inputs[1],
      item_index,
      lowered,
      unsupported,
      &out_item->overlay
    );
    if (status != BB_STATUS_OK) return status;
    out_item->composite.x = (float)info.options.composite.offset_x;
    out_item->composite.y = (float)info.options.composite.offset_y;
    out_item->composite.z = (float)info.options.composite.opacity;
    return BB_STATUS_OK;
  }
  if (info.kind == BB_IMAGE_NODE_OPACITY) {
    bb_image_node_info child = {0};
    status = bb_image_graph_node_info(graph, info.inputs[0], &child);
    if (status != BB_STATUS_OK) return status;
    if (child.kind == BB_IMAGE_NODE_COMPOSITE) {
      bb_sdl_gpu_item child_item = *item;
      child_item.root = info.inputs[0];
      status = bb_sdl_gpu_lower_item(
        graph,
        &child_item,
        item_index,
        lowered,
        unsupported,
        out_item
      );
      if (status == BB_STATUS_OK) out_item->composite.w *= (float)info.options.opacity;
      return status;
    }
  }
  return bb_sdl_gpu_lower_brush(
    graph,
    item->root,
    item_index,
    lowered,
    unsupported,
    &out_item->base
  );
}

bb_status bb_sdl_gpu_lower(
  const bb_image_graph *graph,
  const bb_sdl_gpu_item *items,
  size_t item_count,
  bb_sdl_gpu_unsupported *out_unsupported,
  bb_sdl_gpu_lowered *out_lowered
) {
  bb_sdl_gpu_lowered lowered = {0};
  size_t index;
  if (out_lowered == NULL) return BB_STATUS_INVALID_ARGUMENT;
  memset(out_lowered, 0, sizeof(*out_lowered));
  if (out_unsupported != NULL) memset(out_unsupported, 0, sizeof(*out_unsupported));
  if (graph == NULL || items == NULL || item_count == 0 ||
      item_count > UINT32_MAX || item_count > SIZE_MAX / sizeof(*lowered.items))
    return BB_STATUS_INVALID_ARGUMENT;
  lowered.items = calloc(item_count, sizeof(*lowered.items));
  if (lowered.items == NULL) return BB_STATUS_OUT_OF_MEMORY;
  lowered.item_count = item_count;
  for (index = 0; index < item_count; index += 1) {
    const size_t stop_checkpoint = lowered.stop_count;
    bb_status status;
    if (items[index].root == BB_IMAGE_NODE_NONE ||
        !isfinite(items[index].x) || !isfinite(items[index].y) ||
        !(items[index].width > 0.0f) || !(items[index].height > 0.0f) ||
        !isfinite(items[index].width) || !isfinite(items[index].height)) {
      bb_sdl_gpu_lowered_destroy(&lowered);
      return BB_STATUS_INVALID_ARGUMENT;
    }
    status = bb_sdl_gpu_lower_item(
      graph,
      &items[index],
      index,
      &lowered,
      out_unsupported,
      &lowered.items[index]
    );
    if (status != BB_STATUS_OK) {
      lowered.stop_count = stop_checkpoint;
      bb_sdl_gpu_lowered_destroy(&lowered);
      return status;
    }
  }
  *out_lowered = lowered;
  return BB_STATUS_OK;
}

void bb_sdl_gpu_lowered_destroy(bb_sdl_gpu_lowered *lowered) {
  if (lowered == NULL) return;
  free(lowered->stops);
  free(lowered->items);
  memset(lowered, 0, sizeof(*lowered));
}
