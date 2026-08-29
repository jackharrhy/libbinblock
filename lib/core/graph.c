#include <binblock/graph.h>

#include "checked_math.h"
#include "context_internal.h"
#include "graph_internal.h"

#include <math.h>
#include <string.h>

typedef struct bb_hash_builder {
  uint64_t low;
  uint64_t high;
} bb_hash_builder;

static bb_hash_builder bb_hash_begin(bb_image_node_kind kind) {
  bb_hash_builder hash = {UINT64_C(14695981039346656037), UINT64_C(7809847782465536322)};
  uint32_t value = (uint32_t)kind;
  size_t index;
  for (index = 0; index < 4; index += 1) {
    const uint8_t byte = (uint8_t)(value >> (index * 8));
    hash.low = (hash.low ^ byte) * UINT64_C(1099511628211);
    hash.high = (hash.high ^ (uint8_t)(byte + 0x9d)) * UINT64_C(14029467366897019727);
  }
  return hash;
}

static void bb_hash_byte(bb_hash_builder *hash, uint8_t byte) {
  hash->low = (hash->low ^ byte) * UINT64_C(1099511628211);
  hash->high = (hash->high ^ (uint8_t)(byte + 0x9d)) * UINT64_C(14029467366897019727);
}

static void bb_hash_u32(bb_hash_builder *hash, uint32_t value) {
  size_t index;
  for (index = 0; index < 4; index += 1) bb_hash_byte(hash, (uint8_t)(value >> (index * 8)));
}

static void bb_hash_u64(bb_hash_builder *hash, uint64_t value) {
  size_t index;
  for (index = 0; index < 8; index += 1) bb_hash_byte(hash, (uint8_t)(value >> (index * 8)));
}

static double bb_canonical_double(double value) {
  return value == 0.0 ? 0.0 : value;
}

static void bb_hash_double(bb_hash_builder *hash, double value) {
  uint64_t bits;
  const double canonical = bb_canonical_double(value);
  _Static_assert(sizeof(double) == sizeof(uint64_t), "libbinblock requires binary64-sized doubles");
  memcpy(&bits, &canonical, sizeof(bits));
  bb_hash_u64(hash, bits);
}

static void bb_hash_color(bb_hash_builder *hash, bb_rgba8 color) {
  bb_hash_byte(hash, color.red);
  bb_hash_byte(hash, color.green);
  bb_hash_byte(hash, color.blue);
  bb_hash_byte(hash, color.alpha);
}

static void bb_hash_bytes(bb_hash_builder *hash, const void *bytes, size_t length) {
  const uint8_t *data = bytes;
  size_t index;
  bb_hash_u64(hash, length);
  for (index = 0; index < length; index += 1) bb_hash_byte(hash, data[index]);
}

static void bb_hash_child(bb_hash_builder *hash, const bb_graph_node *child) {
  bb_hash_u64(hash, child->hash.low);
  bb_hash_u64(hash, child->hash.high);
}

static bb_hash128 bb_hash_finish(bb_hash_builder hash) {
  return (bb_hash128){hash.low, hash.high};
}

static int bb_hash_equal(bb_hash128 left, bb_hash128 right) {
  return left.low == right.low && left.high == right.high;
}

static int bb_double_equal(double left, double right) {
  return bb_canonical_double(left) == bb_canonical_double(right);
}

static int bb_unit_value_is_valid(double value) {
  return isfinite(value) && value >= 0.0 && value <= 1.0;
}

static int bb_color_equal(bb_rgba8 left, bb_rgba8 right) {
  return left.red == right.red && left.green == right.green && left.blue == right.blue && left.alpha == right.alpha;
}

static int bb_gradient_stop_equal(const bb_gradient_stop *left, const bb_gradient_stop *right) {
  return bb_double_equal(left->offset, right->offset) && bb_color_equal(left->color, right->color) &&
         !!left->has_easing == !!right->has_easing && (!left->has_easing || left->easing == right->easing);
}

static int bb_graph_node_equal(const bb_graph_node *left, const bb_graph_node *right) {
  size_t index;
  if (left->kind != right->kind || !bb_hash_equal(left->hash, right->hash)) return 0;
  switch (left->kind) {
    case BB_IMAGE_NODE_FILL:
      return left->data.fill.width == right->data.fill.width && left->data.fill.height == right->data.fill.height &&
             bb_color_equal(left->data.fill.color, right->data.fill.color);
    case BB_IMAGE_NODE_ASSET:
      return left->data.asset.desc.width == right->data.asset.desc.width &&
             left->data.asset.desc.height == right->data.asset.desc.height &&
             left->data.asset.content_id_bytes == right->data.asset.content_id_bytes &&
             memcmp(left->data.asset.content_id, right->data.asset.content_id, left->data.asset.content_id_bytes) == 0;
    case BB_IMAGE_NODE_ALPHA_FIELD:
      return left->data.alpha_field.desc.width == right->data.alpha_field.desc.width &&
             left->data.alpha_field.desc.height == right->data.alpha_field.desc.height &&
             left->data.alpha_field.desc.metric == right->data.alpha_field.desc.metric &&
             bb_double_equal(left->data.alpha_field.desc.center_x, right->data.alpha_field.desc.center_x) &&
             bb_double_equal(left->data.alpha_field.desc.center_y, right->data.alpha_field.desc.center_y) &&
             bb_double_equal(left->data.alpha_field.desc.radius, right->data.alpha_field.desc.radius) &&
             left->data.alpha_field.desc.direction == right->data.alpha_field.desc.direction &&
             left->data.alpha_field.desc.easing == right->data.alpha_field.desc.easing &&
             bb_color_equal(left->data.alpha_field.desc.color, right->data.alpha_field.desc.color) &&
             left->data.alpha_field.desc.level_count == right->data.alpha_field.desc.level_count &&
             !!left->data.alpha_field.desc.legacy_radial_rounding == !!right->data.alpha_field.desc.legacy_radial_rounding &&
             (left->data.alpha_field.level_bytes == 0 ||
              memcmp(left->data.alpha_field.levels, right->data.alpha_field.levels, left->data.alpha_field.level_bytes) == 0);
    case BB_IMAGE_NODE_PRESET_GRADIENT:
      return left->data.preset.width == right->data.preset.width && left->data.preset.height == right->data.preset.height &&
             left->data.preset.preset == right->data.preset.preset && left->data.preset.turns == right->data.preset.turns &&
             bb_color_equal(left->data.preset.color, right->data.preset.color);
    case BB_IMAGE_NODE_LINEAR_GRADIENT:
      if (left->data.linear.desc.width != right->data.linear.desc.width ||
          left->data.linear.desc.height != right->data.linear.desc.height ||
          !bb_double_equal(left->data.linear.desc.angle_degrees, right->data.linear.desc.angle_degrees) ||
          !!left->data.linear.desc.has_explicit_extent != !!right->data.linear.desc.has_explicit_extent ||
          (left->data.linear.desc.has_explicit_extent &&
           !bb_double_equal(left->data.linear.desc.extent, right->data.linear.desc.extent)) ||
          left->data.linear.desc.easing != right->data.linear.desc.easing ||
          left->data.linear.desc.stop_count != right->data.linear.desc.stop_count) return 0;
      for (index = 0; index < left->data.linear.desc.stop_count; index += 1)
        if (!bb_gradient_stop_equal(&left->data.linear.stops[index], &right->data.linear.stops[index])) return 0;
      return 1;
    case BB_IMAGE_NODE_ELLIPTICAL_GRADIENT:
      if (left->data.ellipse.desc.width != right->data.ellipse.desc.width ||
          left->data.ellipse.desc.height != right->data.ellipse.desc.height ||
          !bb_double_equal(left->data.ellipse.desc.center_x, right->data.ellipse.desc.center_x) ||
          !bb_double_equal(left->data.ellipse.desc.center_y, right->data.ellipse.desc.center_y) ||
          !bb_double_equal(left->data.ellipse.desc.radius_x, right->data.ellipse.desc.radius_x) ||
          !bb_double_equal(left->data.ellipse.desc.radius_y, right->data.ellipse.desc.radius_y) ||
          !bb_double_equal(left->data.ellipse.desc.rotation_radians, right->data.ellipse.desc.rotation_radians) ||
          left->data.ellipse.desc.easing != right->data.ellipse.desc.easing ||
          !!left->data.ellipse.desc.legacy_radial_rounding != !!right->data.ellipse.desc.legacy_radial_rounding ||
          left->data.ellipse.desc.stop_count != right->data.ellipse.desc.stop_count) return 0;
      for (index = 0; index < left->data.ellipse.desc.stop_count; index += 1)
        if (!bb_gradient_stop_equal(&left->data.ellipse.stops[index], &right->data.ellipse.stops[index])) return 0;
      return 1;
    case BB_IMAGE_NODE_CROP:
      return left->data.crop.source == right->data.crop.source && left->data.crop.x == right->data.crop.x &&
             left->data.crop.y == right->data.crop.y && left->data.crop.width == right->data.crop.width &&
             left->data.crop.height == right->data.crop.height;
    case BB_IMAGE_NODE_CANVAS:
      return left->data.canvas.source == right->data.canvas.source && left->data.canvas.x == right->data.canvas.x &&
             left->data.canvas.y == right->data.canvas.y && left->data.canvas.width == right->data.canvas.width &&
             left->data.canvas.height == right->data.canvas.height;
    case BB_IMAGE_NODE_ROTATE:
      return left->data.rotate.source == right->data.rotate.source && left->data.rotate.turns == right->data.rotate.turns;
    case BB_IMAGE_NODE_OPACITY:
      return left->data.opacity.source == right->data.opacity.source &&
             bb_double_equal(left->data.opacity.opacity, right->data.opacity.opacity);
    case BB_IMAGE_NODE_COMPOSITE:
      return left->data.composite.destination == right->data.composite.destination &&
             left->data.composite.source == right->data.composite.source &&
             left->data.composite.offset_x == right->data.composite.offset_x &&
             left->data.composite.offset_y == right->data.composite.offset_y &&
             bb_double_equal(left->data.composite.opacity, right->data.composite.opacity);
    case BB_IMAGE_NODE_MASK:
      return left->data.mask.source == right->data.mask.source && left->data.mask.mask == right->data.mask.mask &&
             left->data.mask.mode == right->data.mask.mode;
    case BB_IMAGE_NODE_RESIZE:
      return left->data.resize.source == right->data.resize.source && left->data.resize.width == right->data.resize.width &&
             left->data.resize.height == right->data.resize.height;
    case BB_IMAGE_NODE_INVERT_ALPHA:
      return left->data.invert_alpha.source == right->data.invert_alpha.source;
    case BB_IMAGE_NODE_SET_VISIBLE_RGB:
    case BB_IMAGE_NODE_TINT_CHROMA:
      return left->data.color_transform.source == right->data.color_transform.source &&
             bb_color_equal(left->data.color_transform.color, right->data.color_transform.color);
    case BB_IMAGE_NODE_REMAP_TWO_COLOR:
      return left->data.remap.source == right->data.remap.source &&
             bb_color_equal(left->data.remap.source_foreground, right->data.remap.source_foreground) &&
             bb_color_equal(left->data.remap.source_background, right->data.remap.source_background) &&
             bb_color_equal(left->data.remap.foreground, right->data.remap.foreground) &&
             bb_color_equal(left->data.remap.background, right->data.remap.background);
    case BB_IMAGE_NODE_SHIFT_RGB:
      return left->data.shift_rgb.source == right->data.shift_rgb.source &&
             bb_color_equal(left->data.shift_rgb.source_base, right->data.shift_rgb.source_base) &&
             bb_color_equal(left->data.shift_rgb.target_base, right->data.shift_rgb.target_base);
    default:
      return 0;
  }
}

static void bb_graph_node_destroy(bb_context *context, bb_graph_node *node) {
  if (node->kind == BB_IMAGE_NODE_ASSET) {
    bb_context_deallocate(
      context,
      node->data.asset.content_id,
      node->data.asset.content_id_bytes,
      _Alignof(char)
    );
  } else if (node->kind == BB_IMAGE_NODE_ALPHA_FIELD) {
    bb_context_deallocate(context, node->data.alpha_field.levels, node->data.alpha_field.level_bytes, _Alignof(uint8_t));
  } else if (node->kind == BB_IMAGE_NODE_LINEAR_GRADIENT) {
    bb_context_deallocate(context, node->data.linear.stops, node->data.linear.stop_bytes, _Alignof(bb_gradient_stop));
  } else if (node->kind == BB_IMAGE_NODE_ELLIPTICAL_GRADIENT) {
    bb_context_deallocate(context, node->data.ellipse.stops, node->data.ellipse.stop_bytes, _Alignof(bb_gradient_stop));
  }
}

bb_status bb_image_graph_create(bb_context *context, bb_image_graph **out_graph) {
  bb_image_graph *graph;
  bb_status status;
  if (out_graph == NULL) return BB_STATUS_INVALID_ARGUMENT;
  *out_graph = NULL;
  if (context == NULL) return BB_STATUS_INVALID_ARGUMENT;
  status = bb_context_allocate(context, sizeof(*graph), _Alignof(bb_image_graph), (void **)&graph);
  if (status != BB_STATUS_OK) return status;
  memset(graph, 0, sizeof(*graph));
  graph->context = context;
  *out_graph = graph;
  return BB_STATUS_OK;
}

void bb_image_graph_destroy(bb_image_graph *graph) {
  size_t index;
  size_t node_bytes;
  size_t provenance_bytes;
  bb_context *context;
  if (graph == NULL) return;
  context = graph->context;
  for (index = 0; index < graph->node_count; index += 1) bb_graph_node_destroy(context, &graph->nodes[index]);
  node_bytes = graph->node_capacity * sizeof(*graph->nodes);
  provenance_bytes = graph->provenance_capacity * sizeof(*graph->provenance);
  bb_context_deallocate(context, graph->nodes, node_bytes, _Alignof(bb_graph_node));
  bb_context_deallocate(context, graph->provenance, provenance_bytes, _Alignof(bb_graph_provenance));
  bb_context_deallocate(context, graph, sizeof(*graph), _Alignof(bb_image_graph));
}

bb_status bb_image_graph_seal(bb_image_graph *graph) {
  if (graph == NULL) return BB_STATUS_INVALID_ARGUMENT;
  graph->sealed = 1;
  return BB_STATUS_OK;
}

uint32_t bb_image_graph_node_count(const bb_image_graph *graph) {
  return graph == NULL ? 0 : (uint32_t)graph->node_count;
}

const bb_graph_node *bb_image_graph_get_node(const bb_image_graph *graph, bb_image_node node) {
  if (graph == NULL || node == BB_IMAGE_NODE_NONE || node > graph->node_count) return NULL;
  return &graph->nodes[node - 1];
}

bb_status bb_image_graph_node_kind(const bb_image_graph *graph, bb_image_node node, bb_image_node_kind *out_kind) {
  const bb_graph_node *record = bb_image_graph_get_node(graph, node);
  if (out_kind == NULL) return BB_STATUS_INVALID_ARGUMENT;
  if (record == NULL) return BB_STATUS_NOT_FOUND;
  *out_kind = record->kind;
  return BB_STATUS_OK;
}

bb_status bb_image_graph_node_hash(const bb_image_graph *graph, bb_image_node node, bb_hash128 *out_hash) {
  const bb_graph_node *record = bb_image_graph_get_node(graph, node);
  if (out_hash == NULL) return BB_STATUS_INVALID_ARGUMENT;
  if (record == NULL) return BB_STATUS_NOT_FOUND;
  *out_hash = record->hash;
  return BB_STATUS_OK;
}

bb_status bb_image_graph_asset(
  const bb_image_graph *graph,
  bb_image_node node,
  bb_graph_asset_desc *out_desc
) {
  const bb_graph_node *record = bb_image_graph_get_node(graph, node);
  if (out_desc == NULL) return BB_STATUS_INVALID_ARGUMENT;
  if (record == NULL) return BB_STATUS_NOT_FOUND;
  if (record->kind != BB_IMAGE_NODE_ASSET) return BB_STATUS_INVALID_ARGUMENT;
  *out_desc = record->data.asset.desc;
  return BB_STATUS_OK;
}

bb_status bb_image_graph_node_dimensions(
  const bb_image_graph *graph,
  bb_image_node node,
  uint32_t *out_width,
  uint32_t *out_height
) {
  const bb_graph_node *record = bb_image_graph_get_node(graph, node);
  const bb_graph_node *child;
  if (out_width == NULL || out_height == NULL) return BB_STATUS_INVALID_ARGUMENT;
  *out_width = 0;
  *out_height = 0;
  if (record == NULL) return BB_STATUS_NOT_FOUND;
  switch (record->kind) {
    case BB_IMAGE_NODE_FILL:
      *out_width = record->data.fill.width;
      *out_height = record->data.fill.height;
      break;
    case BB_IMAGE_NODE_ASSET:
      *out_width = record->data.asset.desc.width;
      *out_height = record->data.asset.desc.height;
      break;
    case BB_IMAGE_NODE_ALPHA_FIELD:
      *out_width = record->data.alpha_field.desc.width;
      *out_height = record->data.alpha_field.desc.height;
      break;
    case BB_IMAGE_NODE_PRESET_GRADIENT:
      *out_width = record->data.preset.width;
      *out_height = record->data.preset.height;
      break;
    case BB_IMAGE_NODE_LINEAR_GRADIENT:
      *out_width = record->data.linear.desc.width;
      *out_height = record->data.linear.desc.height;
      break;
    case BB_IMAGE_NODE_ELLIPTICAL_GRADIENT:
      *out_width = record->data.ellipse.desc.width;
      *out_height = record->data.ellipse.desc.height;
      break;
    case BB_IMAGE_NODE_CROP:
      *out_width = record->data.crop.width;
      *out_height = record->data.crop.height;
      break;
    case BB_IMAGE_NODE_CANVAS:
      *out_width = record->data.canvas.width;
      *out_height = record->data.canvas.height;
      break;
    case BB_IMAGE_NODE_ROTATE:
      child = bb_image_graph_get_node(graph, record->data.rotate.source);
      if (child == NULL) return BB_STATUS_INTERNAL_ERROR;
      {
        uint32_t child_width;
        uint32_t child_height;
        bb_status status = bb_image_graph_node_dimensions(
          graph,
          record->data.rotate.source,
          &child_width,
          &child_height
        );
        if (status != BB_STATUS_OK) return status;
        if ((record->data.rotate.turns & 1) != 0) {
          *out_width = child_height;
          *out_height = child_width;
        } else {
          *out_width = child_width;
          *out_height = child_height;
        }
      }
      break;
    case BB_IMAGE_NODE_OPACITY:
      return bb_image_graph_node_dimensions(graph, record->data.opacity.source, out_width, out_height);
    case BB_IMAGE_NODE_COMPOSITE:
      return bb_image_graph_node_dimensions(graph, record->data.composite.destination, out_width, out_height);
    case BB_IMAGE_NODE_MASK:
      return bb_image_graph_node_dimensions(graph, record->data.mask.source, out_width, out_height);
    case BB_IMAGE_NODE_RESIZE:
      *out_width = record->data.resize.width;
      *out_height = record->data.resize.height;
      break;
    case BB_IMAGE_NODE_INVERT_ALPHA:
      return bb_image_graph_node_dimensions(graph, record->data.invert_alpha.source, out_width, out_height);
    case BB_IMAGE_NODE_SET_VISIBLE_RGB:
    case BB_IMAGE_NODE_TINT_CHROMA:
      return bb_image_graph_node_dimensions(graph, record->data.color_transform.source, out_width, out_height);
    case BB_IMAGE_NODE_REMAP_TWO_COLOR:
      return bb_image_graph_node_dimensions(graph, record->data.remap.source, out_width, out_height);
    case BB_IMAGE_NODE_SHIFT_RGB:
      return bb_image_graph_node_dimensions(graph, record->data.shift_rgb.source, out_width, out_height);
    default:
      return BB_STATUS_UNSUPPORTED;
  }
  return BB_STATUS_OK;
}

bb_status bb_image_graph_node_inputs(
  const bb_image_graph *graph,
  bb_image_node node,
  bb_image_node *out_inputs,
  size_t capacity,
  size_t *out_count
) {
  const bb_graph_node *item = bb_image_graph_get_node(graph, node);
  bb_image_node inputs[2];
  size_t count = 0;
  if (item == NULL || out_count == NULL || (capacity != 0 && out_inputs == NULL))
    return BB_STATUS_INVALID_ARGUMENT;
  if (item->kind == BB_IMAGE_NODE_CROP) inputs[count++] = item->data.crop.source;
  else if (item->kind == BB_IMAGE_NODE_CANVAS) inputs[count++] = item->data.canvas.source;
  else if (item->kind == BB_IMAGE_NODE_ROTATE) inputs[count++] = item->data.rotate.source;
  else if (item->kind == BB_IMAGE_NODE_OPACITY) inputs[count++] = item->data.opacity.source;
  else if (item->kind == BB_IMAGE_NODE_COMPOSITE) {
    inputs[count++] = item->data.composite.destination;
    inputs[count++] = item->data.composite.source;
  } else if (item->kind == BB_IMAGE_NODE_MASK) {
    inputs[count++] = item->data.mask.source;
    inputs[count++] = item->data.mask.mask;
  } else if (item->kind == BB_IMAGE_NODE_RESIZE) inputs[count++] = item->data.resize.source;
  else if (item->kind == BB_IMAGE_NODE_INVERT_ALPHA) inputs[count++] = item->data.invert_alpha.source;
  else if (item->kind == BB_IMAGE_NODE_SET_VISIBLE_RGB || item->kind == BB_IMAGE_NODE_TINT_CHROMA)
    inputs[count++] = item->data.color_transform.source;
  else if (item->kind == BB_IMAGE_NODE_REMAP_TWO_COLOR) inputs[count++] = item->data.remap.source;
  else if (item->kind == BB_IMAGE_NODE_SHIFT_RGB) inputs[count++] = item->data.shift_rgb.source;
  *out_count = count;
  if (capacity < count) return BB_STATUS_LIMIT_EXCEEDED;
  if (count != 0) memcpy(out_inputs, inputs, count * sizeof(*inputs));
  return BB_STATUS_OK;
}

bb_status bb_image_graph_node_info(
  const bb_image_graph *graph,
  bb_image_node node,
  bb_image_node_info *out_info
) {
  const bb_graph_node *record = bb_image_graph_get_node(graph, node);
  bb_status status;
  if (record == NULL) return BB_STATUS_NOT_FOUND;
  if (out_info == NULL || (out_info->struct_size != 0 && out_info->struct_size != sizeof(*out_info)))
    return BB_STATUS_INVALID_ARGUMENT;
  memset(out_info, 0, sizeof(*out_info));
  out_info->struct_size = sizeof(*out_info);
  out_info->kind = record->kind;
  status = bb_image_graph_node_dimensions(graph, node, &out_info->width, &out_info->height);
  if (status != BB_STATUS_OK) return status;
  status = bb_image_graph_node_inputs(graph, node, out_info->inputs, 2, &out_info->input_count);
  if (status != BB_STATUS_OK) return status;
  switch (record->kind) {
    case BB_IMAGE_NODE_FILL: out_info->options.fill_color = record->data.fill.color; break;
    case BB_IMAGE_NODE_ASSET: out_info->options.asset = record->data.asset.desc; break;
    case BB_IMAGE_NODE_ALPHA_FIELD: out_info->options.alpha_field = record->data.alpha_field.desc; break;
    case BB_IMAGE_NODE_PRESET_GRADIENT:
      out_info->options.preset_gradient.preset = record->data.preset.preset;
      out_info->options.preset_gradient.quarter_turns = record->data.preset.turns;
      out_info->options.preset_gradient.color = record->data.preset.color;
      break;
    case BB_IMAGE_NODE_LINEAR_GRADIENT:
      out_info->options.linear_gradient = record->data.linear.desc;
      break;
    case BB_IMAGE_NODE_ELLIPTICAL_GRADIENT:
      out_info->options.elliptical_gradient = record->data.ellipse.desc;
      break;
    case BB_IMAGE_NODE_CROP:
      out_info->options.placement.x = record->data.crop.x;
      out_info->options.placement.y = record->data.crop.y;
      break;
    case BB_IMAGE_NODE_CANVAS:
      out_info->options.placement.x = record->data.canvas.x;
      out_info->options.placement.y = record->data.canvas.y;
      break;
    case BB_IMAGE_NODE_ROTATE: out_info->options.quarter_turns = record->data.rotate.turns; break;
    case BB_IMAGE_NODE_OPACITY: out_info->options.opacity = record->data.opacity.opacity; break;
    case BB_IMAGE_NODE_COMPOSITE:
      out_info->options.composite.offset_x = record->data.composite.offset_x;
      out_info->options.composite.offset_y = record->data.composite.offset_y;
      out_info->options.composite.opacity = record->data.composite.opacity;
      break;
    case BB_IMAGE_NODE_MASK: out_info->options.mask_mode = record->data.mask.mode; break;
    case BB_IMAGE_NODE_RESIZE: break;
    case BB_IMAGE_NODE_INVERT_ALPHA: break;
    case BB_IMAGE_NODE_SET_VISIBLE_RGB:
    case BB_IMAGE_NODE_TINT_CHROMA:
      out_info->options.color = record->data.color_transform.color;
      break;
    case BB_IMAGE_NODE_REMAP_TWO_COLOR:
      out_info->options.remap.source_foreground = record->data.remap.source_foreground;
      out_info->options.remap.source_background = record->data.remap.source_background;
      out_info->options.remap.foreground = record->data.remap.foreground;
      out_info->options.remap.background = record->data.remap.background;
      break;
    case BB_IMAGE_NODE_SHIFT_RGB:
      out_info->options.shift_rgb.source_base = record->data.shift_rgb.source_base;
      out_info->options.shift_rgb.target_base = record->data.shift_rgb.target_base;
      break;
    default: return BB_STATUS_UNSUPPORTED;
  }
  return BB_STATUS_OK;
}

static bb_status bb_graph_grow_nodes(bb_image_graph *graph, const bb_limits *limits) {
  size_t new_capacity;
  size_t old_bytes;
  size_t new_bytes;
  void *nodes;
  bb_status status;
  if (graph->node_count < graph->node_capacity) return BB_STATUS_OK;
  new_capacity = graph->node_capacity == 0 ? 16 : graph->node_capacity * 2;
  if (new_capacity < graph->node_capacity) return BB_STATUS_OVERFLOW;
  if (new_capacity > limits->max_graph_nodes) new_capacity = (size_t)limits->max_graph_nodes;
  if (new_capacity <= graph->node_capacity) return BB_STATUS_LIMIT_EXCEEDED;
  if (!bb_size_multiply(graph->node_capacity, sizeof(*graph->nodes), &old_bytes) ||
      !bb_size_multiply(new_capacity, sizeof(*graph->nodes), &new_bytes)) return BB_STATUS_OVERFLOW;
  status = bb_context_reallocate(graph->context, graph->nodes, old_bytes, new_bytes, _Alignof(bb_graph_node), &nodes);
  if (status != BB_STATUS_OK) return status;
  graph->nodes = nodes;
  graph->node_capacity = new_capacity;
  return BB_STATUS_OK;
}

static bb_status bb_graph_publish_node(bb_image_graph *graph, bb_graph_node *candidate, bb_image_node *out_node) {
  bb_limits limits;
  size_t index;
  bb_status status;
  if (out_node == NULL) {
    if (graph != NULL) bb_graph_node_destroy(graph->context, candidate);
    return BB_STATUS_INVALID_ARGUMENT;
  }
  *out_node = BB_IMAGE_NODE_NONE;
  if (graph == NULL) return BB_STATUS_INVALID_ARGUMENT;
  if (graph->sealed) {
    bb_graph_node_destroy(graph->context, candidate);
    return BB_STATUS_INVALID_ARGUMENT;
  }
  status = bb_context_get_limits(graph->context, &limits);
  if (status != BB_STATUS_OK) {
    bb_graph_node_destroy(graph->context, candidate);
    return status;
  }
  if (candidate->depth > limits.max_graph_depth || graph->node_count >= limits.max_graph_nodes) {
    bb_graph_node_destroy(graph->context, candidate);
    return BB_STATUS_LIMIT_EXCEEDED;
  }
  for (index = 0; index < graph->node_count; index += 1) {
    if (bb_graph_node_equal(&graph->nodes[index], candidate)) {
      bb_graph_node_destroy(graph->context, candidate);
      *out_node = (bb_image_node)(index + 1);
      return BB_STATUS_OK;
    }
  }
  status = bb_graph_grow_nodes(graph, &limits);
  if (status != BB_STATUS_OK) {
    bb_graph_node_destroy(graph->context, candidate);
    return status;
  }
  graph->nodes[graph->node_count] = *candidate;
  graph->node_count += 1;
  *out_node = (bb_image_node)graph->node_count;
  return BB_STATUS_OK;
}

static bb_status bb_graph_copy_stops(
  bb_image_graph *graph,
  const bb_gradient_stop *stops,
  size_t count,
  bb_gradient_stop **out_stops,
  size_t *out_bytes
) {
  bb_gradient_stop *copy;
  size_t bytes;
  size_t index;
  bb_status status;
  if (stops == NULL || count < 2) return BB_STATUS_INVALID_ARGUMENT;
  if (!bb_size_multiply(count, sizeof(*stops), &bytes)) return BB_STATUS_OVERFLOW;
  status = bb_context_allocate(graph->context, bytes, _Alignof(bb_gradient_stop), (void **)&copy);
  if (status != BB_STATUS_OK) return status;
  memcpy(copy, stops, bytes);
  for (index = 0; index < count; index += 1) {
    bb_gradient_stop value = copy[index];
    size_t cursor = index;
    if (!isfinite(value.offset) || value.offset < 0 || value.offset > 1 ||
        (value.has_easing && (value.easing < BB_EASING_LINEAR || value.easing > BB_EASING_LEGACY))) {
      bb_context_deallocate(graph->context, copy, bytes, _Alignof(bb_gradient_stop));
      return BB_STATUS_INVALID_ARGUMENT;
    }
    value.has_easing = !!value.has_easing;
    value.offset = bb_canonical_double(value.offset);
    while (cursor > 0 && copy[cursor - 1].offset > value.offset) {
      copy[cursor] = copy[cursor - 1];
      cursor -= 1;
    }
    copy[cursor] = value;
  }
  *out_stops = copy;
  *out_bytes = bytes;
  return BB_STATUS_OK;
}

static void bb_hash_stops(bb_hash_builder *hash, const bb_gradient_stop *stops, size_t count) {
  size_t index;
  bb_hash_u64(hash, count);
  for (index = 0; index < count; index += 1) {
    bb_hash_double(hash, stops[index].offset);
    bb_hash_color(hash, stops[index].color);
    bb_hash_u32(hash, !!stops[index].has_easing);
    if (stops[index].has_easing) bb_hash_u32(hash, stops[index].easing);
  }
}

static const bb_graph_node *bb_graph_require_child(const bb_image_graph *graph, bb_image_node child) {
  return bb_image_graph_get_node(graph, child);
}

bb_status bb_image_graph_add_fill(
  bb_image_graph *graph,
  uint32_t width,
  uint32_t height,
  bb_rgba8 color,
  bb_image_node *out_node
) {
  bb_graph_node candidate;
  bb_hash_builder hash = bb_hash_begin(BB_IMAGE_NODE_FILL);
  if (out_node != NULL) *out_node = BB_IMAGE_NODE_NONE;
  if (graph == NULL || width == 0 || height == 0) return BB_STATUS_INVALID_ARGUMENT;
  memset(&candidate, 0, sizeof(candidate));
  candidate.kind = BB_IMAGE_NODE_FILL;
  candidate.depth = 1;
  candidate.data.fill.width = width;
  candidate.data.fill.height = height;
  candidate.data.fill.color = color;
  bb_hash_u32(&hash, width);
  bb_hash_u32(&hash, height);
  bb_hash_color(&hash, color);
  candidate.hash = bb_hash_finish(hash);
  return bb_graph_publish_node(graph, &candidate, out_node);
}

bb_status bb_image_graph_add_asset(
  bb_image_graph *graph,
  const bb_graph_asset_desc *desc,
  bb_image_node *out_node
) {
  bb_graph_node candidate;
  bb_hash_builder hash = bb_hash_begin(BB_IMAGE_NODE_ASSET);
  bb_status status;
  if (out_node != NULL) *out_node = BB_IMAGE_NODE_NONE;
  if (graph == NULL || desc == NULL || desc->content_id.length == 0 || desc->content_id.data == NULL ||
      desc->width == 0 || desc->height == 0) return BB_STATUS_INVALID_ARGUMENT;
  memset(&candidate, 0, sizeof(candidate));
  candidate.kind = BB_IMAGE_NODE_ASSET;
  candidate.depth = 1;
  candidate.data.asset.desc.width = desc->width;
  candidate.data.asset.desc.height = desc->height;
  candidate.data.asset.content_id_bytes = desc->content_id.length;
  status = bb_context_allocate(
    graph->context,
    desc->content_id.length,
    _Alignof(char),
    (void **)&candidate.data.asset.content_id
  );
  if (status != BB_STATUS_OK) return status;
  memcpy(candidate.data.asset.content_id, desc->content_id.data, desc->content_id.length);
  candidate.data.asset.desc.content_id = (bb_string_view){
    candidate.data.asset.content_id,
    candidate.data.asset.content_id_bytes,
  };
  bb_hash_u32(&hash, desc->width);
  bb_hash_u32(&hash, desc->height);
  bb_hash_bytes(&hash, desc->content_id.data, desc->content_id.length);
  candidate.hash = bb_hash_finish(hash);
  return bb_graph_publish_node(graph, &candidate, out_node);
}

bb_status bb_image_graph_add_alpha_field(
  bb_image_graph *graph,
  const bb_alpha_field_desc *desc,
  bb_image_node *out_node
) {
  bb_graph_node candidate;
  bb_hash_builder hash = bb_hash_begin(BB_IMAGE_NODE_ALPHA_FIELD);
  bb_status status;
  size_t index;
  if (out_node != NULL) *out_node = BB_IMAGE_NODE_NONE;
  if (graph == NULL || desc == NULL || desc->width == 0 || desc->height == 0 ||
      desc->metric < BB_ALPHA_METRIC_X || desc->metric > BB_ALPHA_METRIC_BORDER || !isfinite(desc->center_x) ||
      !isfinite(desc->center_y) || !isfinite(desc->radius) || desc->radius == 0.0 ||
      (desc->direction != BB_ALPHA_DIRECTION_OUT && desc->direction != BB_ALPHA_DIRECTION_IN) ||
      desc->easing < BB_EASING_LINEAR || desc->easing > BB_EASING_LEGACY ||
      (desc->level_count != 0 && desc->levels == NULL)) return BB_STATUS_INVALID_ARGUMENT;
  memset(&candidate, 0, sizeof(candidate));
  candidate.kind = BB_IMAGE_NODE_ALPHA_FIELD;
  candidate.depth = 1;
  candidate.data.alpha_field.desc = *desc;
  candidate.data.alpha_field.desc.center_x = bb_canonical_double(desc->center_x);
  candidate.data.alpha_field.desc.center_y = bb_canonical_double(desc->center_y);
  candidate.data.alpha_field.desc.radius = bb_canonical_double(desc->radius);
  candidate.data.alpha_field.desc.legacy_radial_rounding = !!desc->legacy_radial_rounding;
  candidate.data.alpha_field.level_bytes = desc->level_count;
  candidate.data.alpha_field.desc.levels = NULL;
  if (desc->level_count != 0) {
    status = bb_context_allocate(
      graph->context,
      desc->level_count,
      _Alignof(uint8_t),
      (void **)&candidate.data.alpha_field.levels
    );
    if (status != BB_STATUS_OK) return status;
    memcpy(candidate.data.alpha_field.levels, desc->levels, desc->level_count);
    candidate.data.alpha_field.desc.levels = candidate.data.alpha_field.levels;
  }
  bb_hash_u32(&hash, desc->width);
  bb_hash_u32(&hash, desc->height);
  bb_hash_u32(&hash, desc->metric);
  bb_hash_double(&hash, candidate.data.alpha_field.desc.center_x);
  bb_hash_double(&hash, candidate.data.alpha_field.desc.center_y);
  bb_hash_double(&hash, candidate.data.alpha_field.desc.radius);
  bb_hash_u32(&hash, desc->direction);
  bb_hash_u32(&hash, desc->easing);
  bb_hash_color(&hash, desc->color);
  bb_hash_u64(&hash, desc->level_count);
  for (index = 0; index < desc->level_count; index += 1) bb_hash_byte(&hash, desc->levels[index]);
  bb_hash_u32(&hash, !!desc->legacy_radial_rounding);
  candidate.hash = bb_hash_finish(hash);
  return bb_graph_publish_node(graph, &candidate, out_node);
}

bb_status bb_image_graph_add_preset_gradient(
  bb_image_graph *graph,
  uint32_t width,
  uint32_t height,
  bb_gradient_preset preset,
  int32_t quarter_turns,
  bb_rgba8 color,
  bb_image_node *out_node
) {
  bb_graph_node candidate;
  bb_hash_builder hash = bb_hash_begin(BB_IMAGE_NODE_PRESET_GRADIENT);
  const int32_t turns = ((quarter_turns % 4) + 4) % 4;
  if (out_node != NULL) *out_node = BB_IMAGE_NODE_NONE;
  if (graph == NULL || width == 0 || height == 0 || preset < BB_GRADIENT_TOP_DOWN || preset > BB_GRADIENT_CORNER)
    return BB_STATUS_INVALID_ARGUMENT;
  memset(&candidate, 0, sizeof(candidate));
  candidate.kind = BB_IMAGE_NODE_PRESET_GRADIENT;
  candidate.depth = 1;
  candidate.data.preset.width = width;
  candidate.data.preset.height = height;
  candidate.data.preset.preset = preset;
  candidate.data.preset.turns = turns;
  candidate.data.preset.color = color;
  bb_hash_u32(&hash, width);
  bb_hash_u32(&hash, height);
  bb_hash_u32(&hash, preset);
  bb_hash_u32(&hash, (uint32_t)turns);
  bb_hash_color(&hash, color);
  candidate.hash = bb_hash_finish(hash);
  return bb_graph_publish_node(graph, &candidate, out_node);
}

bb_status bb_image_graph_add_linear_gradient(
  bb_image_graph *graph,
  const bb_linear_gradient_desc *desc,
  bb_image_node *out_node
) {
  bb_graph_node candidate;
  bb_hash_builder hash = bb_hash_begin(BB_IMAGE_NODE_LINEAR_GRADIENT);
  bb_status status;
  if (out_node != NULL) *out_node = BB_IMAGE_NODE_NONE;
  if (graph == NULL || desc == NULL || desc->width == 0 || desc->height == 0 || !isfinite(desc->angle_degrees) ||
      desc->easing < BB_EASING_LINEAR || desc->easing > BB_EASING_LEGACY ||
      (desc->has_explicit_extent && (!isfinite(desc->extent) || desc->extent <= 0.0))) return BB_STATUS_INVALID_ARGUMENT;
  memset(&candidate, 0, sizeof(candidate));
  candidate.kind = BB_IMAGE_NODE_LINEAR_GRADIENT;
  candidate.depth = 1;
  status = bb_graph_copy_stops(
    graph,
    desc->stops,
    desc->stop_count,
    &candidate.data.linear.stops,
    &candidate.data.linear.stop_bytes
  );
  if (status != BB_STATUS_OK) return status;
  candidate.data.linear.desc = *desc;
  candidate.data.linear.desc.angle_degrees = bb_canonical_double(desc->angle_degrees);
  candidate.data.linear.desc.extent = bb_canonical_double(desc->extent);
  candidate.data.linear.desc.has_explicit_extent = !!desc->has_explicit_extent;
  candidate.data.linear.desc.stops = candidate.data.linear.stops;
  bb_hash_u32(&hash, desc->width);
  bb_hash_u32(&hash, desc->height);
  bb_hash_double(&hash, candidate.data.linear.desc.angle_degrees);
  bb_hash_u32(&hash, !!desc->has_explicit_extent);
  if (desc->has_explicit_extent) bb_hash_double(&hash, candidate.data.linear.desc.extent);
  bb_hash_u32(&hash, desc->easing);
  bb_hash_stops(&hash, candidate.data.linear.stops, desc->stop_count);
  candidate.hash = bb_hash_finish(hash);
  return bb_graph_publish_node(graph, &candidate, out_node);
}

bb_status bb_image_graph_add_elliptical_gradient(
  bb_image_graph *graph,
  const bb_elliptical_gradient_desc *desc,
  bb_image_node *out_node
) {
  bb_graph_node candidate;
  bb_hash_builder hash = bb_hash_begin(BB_IMAGE_NODE_ELLIPTICAL_GRADIENT);
  bb_status status;
  if (out_node != NULL) *out_node = BB_IMAGE_NODE_NONE;
  if (graph == NULL || desc == NULL || desc->width == 0 || desc->height == 0 || !isfinite(desc->center_x) ||
      !isfinite(desc->center_y) || !isfinite(desc->radius_x) || !isfinite(desc->radius_y) ||
      !isfinite(desc->rotation_radians) || !(desc->radius_x > 0.0) || !(desc->radius_y > 0.0) ||
      desc->easing < BB_EASING_LINEAR || desc->easing > BB_EASING_LEGACY) return BB_STATUS_INVALID_ARGUMENT;
  memset(&candidate, 0, sizeof(candidate));
  candidate.kind = BB_IMAGE_NODE_ELLIPTICAL_GRADIENT;
  candidate.depth = 1;
  status = bb_graph_copy_stops(
    graph,
    desc->stops,
    desc->stop_count,
    &candidate.data.ellipse.stops,
    &candidate.data.ellipse.stop_bytes
  );
  if (status != BB_STATUS_OK) return status;
  candidate.data.ellipse.desc = *desc;
  candidate.data.ellipse.desc.center_x = bb_canonical_double(desc->center_x);
  candidate.data.ellipse.desc.center_y = bb_canonical_double(desc->center_y);
  candidate.data.ellipse.desc.radius_x = bb_canonical_double(desc->radius_x);
  candidate.data.ellipse.desc.radius_y = bb_canonical_double(desc->radius_y);
  candidate.data.ellipse.desc.rotation_radians = bb_canonical_double(desc->rotation_radians);
  candidate.data.ellipse.desc.legacy_radial_rounding = !!desc->legacy_radial_rounding;
  candidate.data.ellipse.desc.stops = candidate.data.ellipse.stops;
  bb_hash_u32(&hash, desc->width);
  bb_hash_u32(&hash, desc->height);
  bb_hash_double(&hash, candidate.data.ellipse.desc.center_x);
  bb_hash_double(&hash, candidate.data.ellipse.desc.center_y);
  bb_hash_double(&hash, candidate.data.ellipse.desc.radius_x);
  bb_hash_double(&hash, candidate.data.ellipse.desc.radius_y);
  bb_hash_double(&hash, candidate.data.ellipse.desc.rotation_radians);
  bb_hash_u32(&hash, desc->easing);
  bb_hash_u32(&hash, !!desc->legacy_radial_rounding);
  bb_hash_stops(&hash, candidate.data.ellipse.stops, desc->stop_count);
  candidate.hash = bb_hash_finish(hash);
  return bb_graph_publish_node(graph, &candidate, out_node);
}

static bb_status bb_graph_begin_unary(
  bb_image_graph *graph,
  bb_image_node source,
  bb_image_node_kind kind,
  bb_graph_node *candidate,
  bb_hash_builder *hash
) {
  const bb_graph_node *child;
  if (graph == NULL || graph->sealed) return BB_STATUS_INVALID_ARGUMENT;
  child = bb_graph_require_child(graph, source);
  if (child == NULL) return BB_STATUS_NOT_FOUND;
  memset(candidate, 0, sizeof(*candidate));
  candidate->kind = kind;
  candidate->depth = child->depth + 1;
  *hash = bb_hash_begin(kind);
  bb_hash_child(hash, child);
  return BB_STATUS_OK;
}

bb_status bb_image_graph_add_crop(
  bb_image_graph *graph,
  bb_image_node source,
  int32_t x,
  int32_t y,
  uint32_t width,
  uint32_t height,
  bb_image_node *out_node
) {
  bb_graph_node candidate;
  bb_hash_builder hash;
  bb_status status;
  if (out_node != NULL) *out_node = BB_IMAGE_NODE_NONE;
  if (width == 0 || height == 0) return BB_STATUS_INVALID_ARGUMENT;
  status = bb_graph_begin_unary(graph, source, BB_IMAGE_NODE_CROP, &candidate, &hash);
  if (status != BB_STATUS_OK) return status;
  candidate.data.crop.source = source;
  candidate.data.crop.x = x;
  candidate.data.crop.y = y;
  candidate.data.crop.width = width;
  candidate.data.crop.height = height;
  bb_hash_u32(&hash, (uint32_t)x);
  bb_hash_u32(&hash, (uint32_t)y);
  bb_hash_u32(&hash, width);
  bb_hash_u32(&hash, height);
  candidate.hash = bb_hash_finish(hash);
  return bb_graph_publish_node(graph, &candidate, out_node);
}

bb_status bb_image_graph_add_canvas(
  bb_image_graph *graph,
  bb_image_node source,
  uint32_t width,
  uint32_t height,
  int32_t x,
  int32_t y,
  bb_image_node *out_node
) {
  bb_graph_node candidate;
  bb_hash_builder hash;
  bb_status status;
  if (out_node != NULL) *out_node = BB_IMAGE_NODE_NONE;
  if (width == 0 || height == 0) return BB_STATUS_INVALID_ARGUMENT;
  status = bb_graph_begin_unary(graph, source, BB_IMAGE_NODE_CANVAS, &candidate, &hash);
  if (status != BB_STATUS_OK) return status;
  candidate.data.canvas.source = source;
  candidate.data.canvas.width = width;
  candidate.data.canvas.height = height;
  candidate.data.canvas.x = x;
  candidate.data.canvas.y = y;
  bb_hash_u32(&hash, width);
  bb_hash_u32(&hash, height);
  bb_hash_u32(&hash, (uint32_t)x);
  bb_hash_u32(&hash, (uint32_t)y);
  candidate.hash = bb_hash_finish(hash);
  return bb_graph_publish_node(graph, &candidate, out_node);
}

bb_status bb_image_graph_add_rotate(
  bb_image_graph *graph,
  bb_image_node source,
  int32_t quarter_turns,
  bb_image_node *out_node
) {
  bb_graph_node candidate;
  bb_hash_builder hash;
  const int32_t turns = ((quarter_turns % 4) + 4) % 4;
  bb_status status;
  if (out_node != NULL) *out_node = BB_IMAGE_NODE_NONE;
  status = bb_graph_begin_unary(graph, source, BB_IMAGE_NODE_ROTATE, &candidate, &hash);
  if (status != BB_STATUS_OK) return status;
  candidate.data.rotate.source = source;
  candidate.data.rotate.turns = turns;
  bb_hash_u32(&hash, (uint32_t)turns);
  candidate.hash = bb_hash_finish(hash);
  return bb_graph_publish_node(graph, &candidate, out_node);
}

bb_status bb_image_graph_add_opacity(
  bb_image_graph *graph,
  bb_image_node source,
  double opacity,
  bb_image_node *out_node
) {
  bb_graph_node candidate;
  bb_hash_builder hash;
  bb_status status;
  if (out_node != NULL) *out_node = BB_IMAGE_NODE_NONE;
  if (!bb_unit_value_is_valid(opacity)) return BB_STATUS_INVALID_ARGUMENT;
  status = bb_graph_begin_unary(graph, source, BB_IMAGE_NODE_OPACITY, &candidate, &hash);
  if (status != BB_STATUS_OK) return status;
  candidate.data.opacity.source = source;
  candidate.data.opacity.opacity = bb_canonical_double(opacity);
  bb_hash_double(&hash, candidate.data.opacity.opacity);
  candidate.hash = bb_hash_finish(hash);
  return bb_graph_publish_node(graph, &candidate, out_node);
}

bb_status bb_image_graph_add_composite(
  bb_image_graph *graph,
  bb_image_node destination,
  bb_image_node source,
  int32_t offset_x,
  int32_t offset_y,
  double opacity,
  bb_image_node *out_node
) {
  const bb_graph_node *destination_node;
  const bb_graph_node *source_node;
  bb_graph_node candidate;
  bb_hash_builder hash = bb_hash_begin(BB_IMAGE_NODE_COMPOSITE);
  if (out_node != NULL) *out_node = BB_IMAGE_NODE_NONE;
  if (graph == NULL || graph->sealed || !bb_unit_value_is_valid(opacity)) return BB_STATUS_INVALID_ARGUMENT;
  destination_node = bb_graph_require_child(graph, destination);
  source_node = bb_graph_require_child(graph, source);
  if (destination_node == NULL || source_node == NULL) return BB_STATUS_NOT_FOUND;
  memset(&candidate, 0, sizeof(candidate));
  candidate.kind = BB_IMAGE_NODE_COMPOSITE;
  candidate.depth = (destination_node->depth > source_node->depth ? destination_node->depth : source_node->depth) + 1;
  candidate.data.composite.destination = destination;
  candidate.data.composite.source = source;
  candidate.data.composite.offset_x = offset_x;
  candidate.data.composite.offset_y = offset_y;
  candidate.data.composite.opacity = bb_canonical_double(opacity);
  bb_hash_child(&hash, destination_node);
  bb_hash_child(&hash, source_node);
  bb_hash_u32(&hash, (uint32_t)offset_x);
  bb_hash_u32(&hash, (uint32_t)offset_y);
  bb_hash_double(&hash, candidate.data.composite.opacity);
  candidate.hash = bb_hash_finish(hash);
  return bb_graph_publish_node(graph, &candidate, out_node);
}

bb_status bb_image_graph_add_mask(
  bb_image_graph *graph,
  bb_image_node source,
  bb_image_node mask,
  bb_mask_mode mode,
  bb_image_node *out_node
) {
  const bb_graph_node *source_node;
  const bb_graph_node *mask_node;
  bb_graph_node candidate;
  bb_hash_builder hash = bb_hash_begin(BB_IMAGE_NODE_MASK);
  if (out_node != NULL) *out_node = BB_IMAGE_NODE_NONE;
  if (graph == NULL || graph->sealed || (mode != BB_MASK_MULTIPLY && mode != BB_MASK_REPLACE))
    return BB_STATUS_INVALID_ARGUMENT;
  source_node = bb_graph_require_child(graph, source);
  mask_node = bb_graph_require_child(graph, mask);
  if (source_node == NULL || mask_node == NULL) return BB_STATUS_NOT_FOUND;
  memset(&candidate, 0, sizeof(candidate));
  candidate.kind = BB_IMAGE_NODE_MASK;
  candidate.depth = (source_node->depth > mask_node->depth ? source_node->depth : mask_node->depth) + 1;
  candidate.data.mask.source = source;
  candidate.data.mask.mask = mask;
  candidate.data.mask.mode = mode;
  bb_hash_child(&hash, source_node);
  bb_hash_child(&hash, mask_node);
  bb_hash_u32(&hash, mode);
  candidate.hash = bb_hash_finish(hash);
  return bb_graph_publish_node(graph, &candidate, out_node);
}

bb_status bb_image_graph_add_resize(
  bb_image_graph *graph,
  bb_image_node source,
  uint32_t width,
  uint32_t height,
  bb_image_node *out_node
) {
  bb_graph_node candidate;
  bb_hash_builder hash;
  bb_status status;
  if (out_node != NULL) *out_node = BB_IMAGE_NODE_NONE;
  if (width == 0 || height == 0) return BB_STATUS_INVALID_ARGUMENT;
  status = bb_graph_begin_unary(graph, source, BB_IMAGE_NODE_RESIZE, &candidate, &hash);
  if (status != BB_STATUS_OK) return status;
  candidate.data.resize.source = source;
  candidate.data.resize.width = width;
  candidate.data.resize.height = height;
  bb_hash_u32(&hash, width);
  bb_hash_u32(&hash, height);
  candidate.hash = bb_hash_finish(hash);
  return bb_graph_publish_node(graph, &candidate, out_node);
}

bb_status bb_image_graph_add_invert_alpha(
  bb_image_graph *graph,
  bb_image_node source,
  bb_image_node *out_node
) {
  bb_graph_node candidate;
  bb_hash_builder hash;
  bb_status status;
  if (out_node != NULL) *out_node = BB_IMAGE_NODE_NONE;
  status = bb_graph_begin_unary(graph, source, BB_IMAGE_NODE_INVERT_ALPHA, &candidate, &hash);
  if (status != BB_STATUS_OK) return status;
  candidate.data.invert_alpha.source = source;
  candidate.hash = bb_hash_finish(hash);
  return bb_graph_publish_node(graph, &candidate, out_node);
}

static bb_status bb_image_graph_add_color_transform(
  bb_image_graph *graph,
  bb_image_node source,
  bb_rgba8 color,
  bb_image_node_kind kind,
  bb_image_node *out_node
) {
  bb_graph_node candidate;
  bb_hash_builder hash;
  bb_status status;
  if (out_node != NULL) *out_node = BB_IMAGE_NODE_NONE;
  status = bb_graph_begin_unary(graph, source, kind, &candidate, &hash);
  if (status != BB_STATUS_OK) return status;
  candidate.data.color_transform.source = source;
  candidate.data.color_transform.color = color;
  bb_hash_color(&hash, color);
  candidate.hash = bb_hash_finish(hash);
  return bb_graph_publish_node(graph, &candidate, out_node);
}

bb_status bb_image_graph_add_set_visible_rgb(
  bb_image_graph *graph,
  bb_image_node source,
  bb_rgba8 color,
  bb_image_node *out_node
) {
  return bb_image_graph_add_color_transform(graph, source, color, BB_IMAGE_NODE_SET_VISIBLE_RGB, out_node);
}

bb_status bb_image_graph_add_tint_chroma(
  bb_image_graph *graph,
  bb_image_node source,
  bb_rgba8 color,
  bb_image_node *out_node
) {
  return bb_image_graph_add_color_transform(graph, source, color, BB_IMAGE_NODE_TINT_CHROMA, out_node);
}

bb_status bb_image_graph_add_remap_two_color(
  bb_image_graph *graph,
  bb_image_node source,
  bb_rgba8 source_foreground,
  bb_rgba8 source_background,
  bb_rgba8 foreground,
  bb_rgba8 background,
  bb_image_node *out_node
) {
  bb_graph_node candidate;
  bb_hash_builder hash;
  bb_status status;
  if (out_node != NULL) *out_node = BB_IMAGE_NODE_NONE;
  status = bb_graph_begin_unary(graph, source, BB_IMAGE_NODE_REMAP_TWO_COLOR, &candidate, &hash);
  if (status != BB_STATUS_OK) return status;
  candidate.data.remap.source = source;
  candidate.data.remap.source_foreground = source_foreground;
  candidate.data.remap.source_background = source_background;
  candidate.data.remap.foreground = foreground;
  candidate.data.remap.background = background;
  bb_hash_color(&hash, source_foreground);
  bb_hash_color(&hash, source_background);
  bb_hash_color(&hash, foreground);
  bb_hash_color(&hash, background);
  candidate.hash = bb_hash_finish(hash);
  return bb_graph_publish_node(graph, &candidate, out_node);
}

bb_status bb_image_graph_add_shift_rgb(
  bb_image_graph *graph,
  bb_image_node source,
  bb_rgba8 source_base,
  bb_rgba8 target_base,
  bb_image_node *out_node
) {
  bb_graph_node candidate;
  bb_hash_builder hash;
  bb_status status;
  if (out_node != NULL) *out_node = BB_IMAGE_NODE_NONE;
  status = bb_graph_begin_unary(graph, source, BB_IMAGE_NODE_SHIFT_RGB, &candidate, &hash);
  if (status != BB_STATUS_OK) return status;
  candidate.data.shift_rgb.source = source;
  candidate.data.shift_rgb.source_base = source_base;
  candidate.data.shift_rgb.target_base = target_base;
  bb_hash_color(&hash, source_base);
  bb_hash_color(&hash, target_base);
  candidate.hash = bb_hash_finish(hash);
  return bb_graph_publish_node(graph, &candidate, out_node);
}

static bb_status bb_graph_grow_provenance(bb_image_graph *graph) {
  size_t new_capacity;
  size_t old_bytes;
  size_t new_bytes;
  void *records;
  bb_status status;
  if (graph->provenance_count < graph->provenance_capacity) return BB_STATUS_OK;
  new_capacity = graph->provenance_capacity == 0 ? 16 : graph->provenance_capacity * 2;
  if (new_capacity < graph->provenance_capacity) return BB_STATUS_OVERFLOW;
  if (!bb_size_multiply(graph->provenance_capacity, sizeof(*graph->provenance), &old_bytes) ||
      !bb_size_multiply(new_capacity, sizeof(*graph->provenance), &new_bytes)) return BB_STATUS_OVERFLOW;
  status = bb_context_reallocate(
    graph->context,
    graph->provenance,
    old_bytes,
    new_bytes,
    _Alignof(bb_graph_provenance),
    &records
  );
  if (status != BB_STATUS_OK) return status;
  graph->provenance = records;
  graph->provenance_capacity = new_capacity;
  return BB_STATUS_OK;
}

bb_status bb_image_graph_attach_span(bb_image_graph *graph, bb_image_node node, bb_span span) {
  bb_limits limits;
  size_t index;
  bb_status status;
  if (graph == NULL || graph->sealed) return BB_STATUS_INVALID_ARGUMENT;
  if (bb_image_graph_get_node(graph, node) == NULL) return BB_STATUS_NOT_FOUND;
  status = bb_context_validate_span(graph->context, span);
  if (status != BB_STATUS_OK) return status;
  for (index = 0; index < graph->provenance_count; index += 1) {
    const bb_graph_provenance *existing = &graph->provenance[index];
    if (existing->node == node && existing->span.source_id == span.source_id &&
        existing->span.byte_start == span.byte_start && existing->span.byte_end == span.byte_end) return BB_STATUS_OK;
  }
  status = bb_context_get_limits(graph->context, &limits);
  if (status != BB_STATUS_OK) return status;
  if (graph->provenance_count >= limits.max_provenance_records) return BB_STATUS_LIMIT_EXCEEDED;
  status = bb_graph_grow_provenance(graph);
  if (status != BB_STATUS_OK) return status;
  graph->provenance[graph->provenance_count].node = node;
  graph->provenance[graph->provenance_count].span = span;
  graph->provenance_count += 1;
  return BB_STATUS_OK;
}

size_t bb_image_graph_provenance_count(const bb_image_graph *graph, bb_image_node node) {
  size_t count = 0;
  size_t index;
  if (bb_image_graph_get_node(graph, node) == NULL) return 0;
  for (index = 0; index < graph->provenance_count; index += 1)
    if (graph->provenance[index].node == node) count += 1;
  return count;
}

bb_status bb_image_graph_provenance(
  const bb_image_graph *graph,
  bb_image_node node,
  size_t requested_index,
  bb_span *out_span
) {
  size_t found = 0;
  size_t index;
  if (graph == NULL || out_span == NULL) return BB_STATUS_INVALID_ARGUMENT;
  if (bb_image_graph_get_node(graph, node) == NULL) return BB_STATUS_NOT_FOUND;
  for (index = 0; index < graph->provenance_count; index += 1) {
    if (graph->provenance[index].node != node) continue;
    if (found == requested_index) {
      *out_span = graph->provenance[index].span;
      return BB_STATUS_OK;
    }
    found += 1;
  }
  return BB_STATUS_NOT_FOUND;
}
