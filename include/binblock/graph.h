#ifndef BINBLOCK_GRAPH_H
#define BINBLOCK_GRAPH_H

#include <binblock/raster.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct bb_image_graph bb_image_graph;
typedef uint32_t bb_image_node;

#define BB_IMAGE_NODE_NONE ((bb_image_node)0)

typedef enum bb_image_node_kind {
  BB_IMAGE_NODE_FILL = 1,
  BB_IMAGE_NODE_ALPHA_FIELD = 2,
  BB_IMAGE_NODE_PRESET_GRADIENT = 3,
  BB_IMAGE_NODE_LINEAR_GRADIENT = 4,
  BB_IMAGE_NODE_ELLIPTICAL_GRADIENT = 5,
  BB_IMAGE_NODE_CROP = 6,
  BB_IMAGE_NODE_CANVAS = 7,
  BB_IMAGE_NODE_ROTATE = 8,
  BB_IMAGE_NODE_OPACITY = 9,
  BB_IMAGE_NODE_COMPOSITE = 10,
  BB_IMAGE_NODE_MASK = 11,
  BB_IMAGE_NODE_RESIZE = 12,
  BB_IMAGE_NODE_ASSET = 13,
  BB_IMAGE_NODE_INVERT_ALPHA = 14,
  BB_IMAGE_NODE_SET_VISIBLE_RGB = 15,
  BB_IMAGE_NODE_TINT_CHROMA = 16,
  BB_IMAGE_NODE_REMAP_TWO_COLOR = 17,
  BB_IMAGE_NODE_SHIFT_RGB = 18
} bb_image_node_kind;

typedef struct bb_hash128 {
  uint64_t low;
  uint64_t high;
} bb_hash128;

typedef struct bb_graph_asset_desc {
  /* Stable content identity, such as a cryptographic hash; never a host path. */
  bb_string_view content_id;
  uint32_t width;
  uint32_t height;
} bb_graph_asset_desc;

typedef bb_status (*bb_graph_asset_decode_fn)(
  void *user,
  bb_string_view content_id,
  bb_const_image_view *out_image
);

/* Hosts may use this callback to project clocks, deadlines, task supersession,
 * or user cancellation into the portable renderer. Return non-zero to stop. */
typedef uint32_t (*bb_render_should_cancel_fn)(void *user);

typedef struct bb_render_options {
  uint32_t struct_size;
  void *user;
  bb_render_should_cancel_fn should_cancel;
  /* Sum of output pixels for the graph nodes required by one render. */
  uint64_t max_work_units;
} bb_render_options;

/* Borrowed semantic description for host/backend lowering. Any pointer inside
 * an embedded raster descriptor remains valid only for the graph lifetime. */
typedef struct bb_image_node_info {
  uint32_t struct_size;
  bb_image_node_kind kind;
  uint32_t width;
  uint32_t height;
  bb_image_node inputs[2];
  size_t input_count;
  union {
    bb_rgba8 fill_color;
    bb_graph_asset_desc asset;
    bb_alpha_field_desc alpha_field;
    struct {
      bb_gradient_preset preset;
      int32_t quarter_turns;
      bb_rgba8 color;
    } preset_gradient;
    bb_linear_gradient_desc linear_gradient;
    bb_elliptical_gradient_desc elliptical_gradient;
    struct { int32_t x; int32_t y; } placement;
    int32_t quarter_turns;
    double opacity;
    struct { int32_t offset_x; int32_t offset_y; double opacity; } composite;
    bb_mask_mode mask_mode;
    bb_rgba8 color;
    struct {
      bb_rgba8 source_foreground;
      bb_rgba8 source_background;
      bb_rgba8 foreground;
      bb_rgba8 background;
    } remap;
    struct { bb_rgba8 source_base; bb_rgba8 target_base; } shift_rgb;
  } options;
} bb_image_node_info;

BB_API bb_status bb_image_graph_create(bb_context *context, bb_image_graph **out_graph);
BB_API void bb_image_graph_destroy(bb_image_graph *graph);
BB_API bb_status bb_image_graph_seal(bb_image_graph *graph);
BB_API uint32_t bb_image_graph_node_count(const bb_image_graph *graph);
BB_API bb_status bb_image_graph_node_kind(
  const bb_image_graph *graph,
  bb_image_node node,
  bb_image_node_kind *out_kind
);
BB_API bb_status bb_image_graph_node_hash(const bb_image_graph *graph, bb_image_node node, bb_hash128 *out_hash);
BB_API bb_status bb_image_graph_asset(
  const bb_image_graph *graph,
  bb_image_node node,
  bb_graph_asset_desc *out_desc
);
BB_API bb_status bb_image_graph_node_dimensions(
  const bb_image_graph *graph,
  bb_image_node node,
  uint32_t *out_width,
  uint32_t *out_height
);
BB_API bb_status bb_image_graph_node_info(
  const bb_image_graph *graph,
  bb_image_node node,
  bb_image_node_info *out_info
);
/* Queries ordered graph inputs. out_count is always set to the required count;
 * pass NULL/0 to measure before copying. */
BB_API bb_status bb_image_graph_node_inputs(
  const bb_image_graph *graph,
  bb_image_node node,
  bb_image_node *out_inputs,
  size_t capacity,
  size_t *out_count
);
BB_API bb_status bb_image_graph_attach_span(bb_image_graph *graph, bb_image_node node, bb_span span);
BB_API size_t bb_image_graph_provenance_count(const bb_image_graph *graph, bb_image_node node);
BB_API bb_status bb_image_graph_provenance(
  const bb_image_graph *graph,
  bb_image_node node,
  size_t index,
  bb_span *out_span
);

BB_API bb_status bb_image_graph_add_fill(
  bb_image_graph *graph,
  uint32_t width,
  uint32_t height,
  bb_rgba8 color,
  bb_image_node *out_node
);
BB_API bb_status bb_image_graph_add_asset(
  bb_image_graph *graph,
  const bb_graph_asset_desc *desc,
  bb_image_node *out_node
);
BB_API bb_status bb_image_graph_add_alpha_field(
  bb_image_graph *graph,
  const bb_alpha_field_desc *desc,
  bb_image_node *out_node
);
BB_API bb_status bb_image_graph_add_preset_gradient(
  bb_image_graph *graph,
  uint32_t width,
  uint32_t height,
  bb_gradient_preset preset,
  int32_t quarter_turns,
  bb_rgba8 color,
  bb_image_node *out_node
);
BB_API bb_status bb_image_graph_add_linear_gradient(
  bb_image_graph *graph,
  const bb_linear_gradient_desc *desc,
  bb_image_node *out_node
);
BB_API bb_status bb_image_graph_add_elliptical_gradient(
  bb_image_graph *graph,
  const bb_elliptical_gradient_desc *desc,
  bb_image_node *out_node
);
BB_API bb_status bb_image_graph_add_crop(
  bb_image_graph *graph,
  bb_image_node source,
  int32_t x,
  int32_t y,
  uint32_t width,
  uint32_t height,
  bb_image_node *out_node
);
BB_API bb_status bb_image_graph_add_canvas(
  bb_image_graph *graph,
  bb_image_node source,
  uint32_t width,
  uint32_t height,
  int32_t x,
  int32_t y,
  bb_image_node *out_node
);
BB_API bb_status bb_image_graph_add_rotate(
  bb_image_graph *graph,
  bb_image_node source,
  int32_t quarter_turns,
  bb_image_node *out_node
);
BB_API bb_status bb_image_graph_add_opacity(
  bb_image_graph *graph,
  bb_image_node source,
  double opacity,
  bb_image_node *out_node
);
BB_API bb_status bb_image_graph_add_composite(
  bb_image_graph *graph,
  bb_image_node destination,
  bb_image_node source,
  int32_t offset_x,
  int32_t offset_y,
  double opacity,
  bb_image_node *out_node
);
BB_API bb_status bb_image_graph_add_mask(
  bb_image_graph *graph,
  bb_image_node source,
  bb_image_node mask,
  bb_mask_mode mode,
  bb_image_node *out_node
);
BB_API bb_status bb_image_graph_add_resize(
  bb_image_graph *graph,
  bb_image_node source,
  uint32_t width,
  uint32_t height,
  bb_image_node *out_node
);
BB_API bb_status bb_image_graph_add_invert_alpha(
  bb_image_graph *graph,
  bb_image_node source,
  bb_image_node *out_node
);
BB_API bb_status bb_image_graph_add_set_visible_rgb(
  bb_image_graph *graph,
  bb_image_node source,
  bb_rgba8 color,
  bb_image_node *out_node
);
BB_API bb_status bb_image_graph_add_tint_chroma(
  bb_image_graph *graph,
  bb_image_node source,
  bb_rgba8 color,
  bb_image_node *out_node
);
BB_API bb_status bb_image_graph_add_remap_two_color(
  bb_image_graph *graph,
  bb_image_node source,
  bb_rgba8 source_foreground,
  bb_rgba8 source_background,
  bb_rgba8 foreground,
  bb_rgba8 background,
  bb_image_node *out_node
);
BB_API bb_status bb_image_graph_add_shift_rgb(
  bb_image_graph *graph,
  bb_image_node source,
  bb_rgba8 source_base,
  bb_rgba8 target_base,
  bb_image_node *out_node
);

BB_API void bb_render_options_init(bb_render_options *options);

/* Renders one published root through the canonical software rasterizer. Graph
 * construction must not occur concurrently with this call. */
BB_API bb_status bb_image_graph_render_raster(
  bb_context *context,
  const bb_image_graph *graph,
  bb_image_node root,
  bb_surface **out_surface
);
BB_API bb_status bb_image_graph_render_raster_with_assets(
  bb_context *context,
  const bb_image_graph *graph,
  bb_image_node root,
  bb_graph_asset_decode_fn decode,
  void *user,
  bb_surface **out_surface
);
BB_API bb_status bb_image_graph_render_raster_with_options(
  bb_context *context,
  const bb_image_graph *graph,
  bb_image_node root,
  const bb_render_options *options,
  bb_graph_asset_decode_fn decode,
  void *decode_user,
  bb_surface **out_surface
);

#ifdef __cplusplus
}
#endif

#endif
