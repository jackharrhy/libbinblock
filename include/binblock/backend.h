#ifndef BINBLOCK_BACKEND_H
#define BINBLOCK_BACKEND_H

#include <binblock/graph.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct bb_render_plan bb_render_plan;

typedef enum bb_backend_equivalence {
  BB_BACKEND_EQUIVALENCE_EXACT = 0,
  BB_BACKEND_EQUIVALENCE_BOUNDED = 1,
  BB_BACKEND_EQUIVALENCE_VISUAL = 2,
  BB_BACKEND_EQUIVALENCE_UNSUPPORTED = 3
} bb_backend_equivalence;

typedef enum bb_backend_flag {
  BB_BACKEND_CAN_UPLOAD = 1u << 0,
  BB_BACKEND_CAN_READBACK = 1u << 1,
  BB_BACKEND_DYNAMIC_FIELDS = 1u << 2
} bb_backend_flag;

typedef struct bb_backend_capabilities {
  uint32_t struct_size;
  uint32_t flags;
  uint64_t supported_node_kinds;
  uint64_t exact_node_kinds;
  uint64_t bounded_node_kinds;
  uint64_t dynamic_node_kinds;
  uint32_t bounded_max_channel_error;
  uint32_t max_texture_width;
  uint32_t max_texture_height;
  uint32_t max_resources;
  uint32_t preferred_tile_width;
  uint32_t preferred_tile_height;
  uint32_t row_alignment;
} bb_backend_capabilities;

typedef bb_status (*bb_backend_query_node_fn)(
  void *user,
  const bb_image_graph *graph,
  bb_image_node node,
  bb_backend_equivalence *out_equivalence
);

typedef bb_status (*bb_backend_upload_baked_fn)(
  void *user,
  bb_image_node replaced_node,
  bb_hash128 structural_hash,
  const bb_surface *surface
);

typedef bb_status (*bb_backend_render_direct_fn)(
  void *user,
  const bb_image_graph *graph,
  bb_image_node root,
  bb_surface **out_readback
);

typedef struct bb_backend_interface {
  uint32_t struct_size;
  void *user;
  bb_backend_capabilities capabilities;
  bb_backend_query_node_fn query_node;
  bb_backend_upload_baked_fn upload_baked;
  bb_backend_render_direct_fn render_direct;
} bb_backend_interface;

typedef struct bb_render_plan_options {
  uint32_t struct_size;
  uint32_t allow_cpu_fallback;
  uint64_t max_baked_bytes;
} bb_render_plan_options;

typedef enum bb_render_plan_step_kind {
  BB_RENDER_PLAN_CPU_BAKE_UPLOAD = 1,
  BB_RENDER_PLAN_BACKEND_DIRECT = 2,
  BB_RENDER_PLAN_CPU_FINAL = 3
} bb_render_plan_step_kind;

typedef struct bb_render_plan_step {
  bb_render_plan_step_kind kind;
  bb_image_node root;
  bb_hash128 structural_hash;
  uint32_t width;
  uint32_t height;
  bb_backend_equivalence equivalence;
  uint32_t max_channel_error;
} bb_render_plan_step;

BB_API void bb_backend_capabilities_init(bb_backend_capabilities *capabilities);
BB_API void bb_backend_interface_init(bb_backend_interface *backend);
BB_API void bb_render_plan_options_init(bb_render_plan_options *options);
BB_API uint64_t bb_backend_node_kind_bit(bb_image_node_kind kind);
BB_API void bb_backend_capabilities_webgl2(bb_backend_capabilities *capabilities);
BB_API void bb_backend_capabilities_webgpu(bb_backend_capabilities *capabilities);
BB_API void bb_backend_capabilities_godot_rendering_device(bb_backend_capabilities *capabilities);
BB_API void bb_backend_capabilities_wii_gx(bb_backend_capabilities *capabilities);

BB_API bb_status bb_render_plan_create(
  bb_context *context,
  const bb_image_graph *graph,
  bb_image_node root,
  const bb_backend_interface *backend,
  const bb_render_plan_options *options,
  bb_render_plan **out_plan
);
BB_API void bb_render_plan_destroy(bb_render_plan *plan);
BB_API size_t bb_render_plan_step_count(const bb_render_plan *plan);
BB_API bb_status bb_render_plan_step_get(
  const bb_render_plan *plan,
  size_t index,
  bb_render_plan_step *out_step
);
BB_API bb_backend_equivalence bb_render_plan_equivalence(const bb_render_plan *plan);
BB_API uint32_t bb_render_plan_max_channel_error(const bb_render_plan *plan);
BB_API uint64_t bb_render_plan_baked_bytes(const bb_render_plan *plan);

/* Convenience executor for readback-oriented hosts and tests. Device-only hosts
 * can consume the plan steps directly and retain their own resources. */
BB_API bb_status bb_render_plan_execute(
  bb_context *context,
  const bb_image_graph *graph,
  const bb_render_plan *plan,
  const bb_backend_interface *backend,
  bb_graph_asset_decode_fn decode_asset,
  void *decode_user,
  bb_surface **out_surface
);
BB_API bb_status bb_render_plan_execute_with_options(
  bb_context *context,
  const bb_image_graph *graph,
  const bb_render_plan *plan,
  const bb_backend_interface *backend,
  const bb_render_options *options,
  bb_graph_asset_decode_fn decode_asset,
  void *decode_user,
  bb_surface **out_surface
);

#ifdef __cplusplus
}
#endif

#endif
