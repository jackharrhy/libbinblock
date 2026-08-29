#include <binblock/backend.h>

#include "context_internal.h"

#include <string.h>

struct bb_render_plan {
  bb_context *context;
  bb_render_plan_step *steps;
  size_t step_count;
  size_t step_capacity;
  bb_backend_equivalence equivalence;
  uint32_t max_channel_error;
  uint64_t baked_bytes;
};

typedef struct bb_plan_builder {
  bb_context *context;
  const bb_image_graph *graph;
  const bb_backend_interface *backend;
  const bb_render_plan_options *options;
  bb_render_plan *plan;
  uint8_t *state;
  uint8_t *direct;
  uint8_t *step_added;
  size_t node_slots;
  size_t scratch_bytes;
  size_t upload_count;
} bb_plan_builder;

uint64_t bb_backend_node_kind_bit(bb_image_node_kind kind) {
  if (kind < BB_IMAGE_NODE_FILL || kind > 64) return 0;
  return UINT64_C(1) << ((uint32_t)kind - 1);
}

void bb_backend_capabilities_init(bb_backend_capabilities *capabilities) {
  if (capabilities == NULL) return;
  memset(capabilities, 0, sizeof(*capabilities));
  capabilities->struct_size = (uint32_t)sizeof(*capabilities);
  capabilities->max_texture_width = UINT32_MAX;
  capabilities->max_texture_height = UINT32_MAX;
  capabilities->max_resources = UINT32_MAX;
  capabilities->preferred_tile_width = 1;
  capabilities->preferred_tile_height = 1;
  capabilities->row_alignment = 1;
}

void bb_backend_interface_init(bb_backend_interface *backend) {
  if (backend == NULL) return;
  memset(backend, 0, sizeof(*backend));
  backend->struct_size = (uint32_t)sizeof(*backend);
  bb_backend_capabilities_init(&backend->capabilities);
}

void bb_render_plan_options_init(bb_render_plan_options *options) {
  if (options == NULL) return;
  memset(options, 0, sizeof(*options));
  options->struct_size = (uint32_t)sizeof(*options);
  options->allow_cpu_fallback = 1;
  options->max_baked_bytes = UINT64_MAX;
}

static bb_status bb_plan_query_node(
  const bb_plan_builder *builder,
  bb_image_node node,
  bb_backend_equivalence *out_equivalence
) {
  bb_image_node_kind kind;
  uint32_t width;
  uint32_t height;
  uint64_t bit;
  bb_backend_equivalence equivalence;
  bb_status status = bb_image_graph_node_kind(builder->graph, node, &kind);
  if (status == BB_STATUS_OK) status = bb_image_graph_node_dimensions(builder->graph, node, &width, &height);
  if (status != BB_STATUS_OK) return status;
  bit = bb_backend_node_kind_bit(kind);
  if ((builder->backend->capabilities.supported_node_kinds & bit) == 0 ||
      width > builder->backend->capabilities.max_texture_width ||
      height > builder->backend->capabilities.max_texture_height) {
    *out_equivalence = BB_BACKEND_EQUIVALENCE_UNSUPPORTED;
    return BB_STATUS_OK;
  }
  if ((builder->backend->capabilities.exact_node_kinds & bit) != 0)
    equivalence = BB_BACKEND_EQUIVALENCE_EXACT;
  else if ((builder->backend->capabilities.bounded_node_kinds & bit) != 0)
    equivalence = BB_BACKEND_EQUIVALENCE_BOUNDED;
  else equivalence = BB_BACKEND_EQUIVALENCE_VISUAL;
  if (builder->backend->query_node != NULL) {
    status = builder->backend->query_node(builder->backend->user, builder->graph, node, &equivalence);
    if (status != BB_STATUS_OK) return status;
    if (equivalence < BB_BACKEND_EQUIVALENCE_EXACT ||
        equivalence > BB_BACKEND_EQUIVALENCE_UNSUPPORTED) return BB_STATUS_INVALID_ARGUMENT;
  }
  *out_equivalence = equivalence;
  return BB_STATUS_OK;
}

static bb_status bb_plan_add_step(
  bb_plan_builder *builder,
  bb_render_plan_step_kind kind,
  bb_image_node root,
  bb_backend_equivalence equivalence,
  uint32_t max_error
) {
  bb_render_plan_step *step;
  uint32_t width;
  uint32_t height;
  uint64_t bytes;
  bb_status status;
  if (builder->plan->step_count >= builder->plan->step_capacity) return BB_STATUS_LIMIT_EXCEEDED;
  status = bb_image_graph_node_dimensions(builder->graph, root, &width, &height);
  if (status == BB_STATUS_OK)
    status = bb_image_graph_node_hash(builder->graph, root, &builder->plan->steps[builder->plan->step_count].structural_hash);
  if (status != BB_STATUS_OK) return status;
  if (kind != BB_RENDER_PLAN_BACKEND_DIRECT) {
    bytes = (uint64_t)width * height * 4;
    if (UINT64_MAX - builder->plan->baked_bytes < bytes ||
        builder->plan->baked_bytes + bytes > builder->options->max_baked_bytes)
      return BB_STATUS_LIMIT_EXCEEDED;
    builder->plan->baked_bytes += bytes;
  }
  step = &builder->plan->steps[builder->plan->step_count++];
  step->kind = kind;
  step->root = root;
  step->width = width;
  step->height = height;
  step->equivalence = equivalence;
  step->max_channel_error = max_error;
  return BB_STATUS_OK;
}

static void bb_plan_accumulate_equivalence(
  bb_render_plan *plan,
  bb_backend_equivalence equivalence,
  uint32_t max_error
) {
  if (equivalence > plan->equivalence) plan->equivalence = equivalence;
  if (max_error > plan->max_channel_error) plan->max_channel_error = max_error;
}

static bb_status bb_plan_analyze(bb_plan_builder *builder, bb_image_node node) {
  bb_backend_equivalence equivalence;
  bb_image_node inputs[2];
  size_t input_count = 0;
  size_t index;
  bb_status status;
  if (node == BB_IMAGE_NODE_NONE || node >= builder->node_slots) return BB_STATUS_NOT_FOUND;
  if (builder->state[node] == 2) return BB_STATUS_OK;
  if (builder->state[node] == 1) return BB_STATUS_INTERNAL_ERROR;
  builder->state[node] = 1;
  status = bb_plan_query_node(builder, node, &equivalence);
  if (status != BB_STATUS_OK) return status;
  if (equivalence == BB_BACKEND_EQUIVALENCE_UNSUPPORTED) {
    builder->direct[node] = 0;
    builder->state[node] = 2;
    return BB_STATUS_OK;
  }
  status = bb_image_graph_node_inputs(builder->graph, node, inputs, 2, &input_count);
  if (status != BB_STATUS_OK) return status;
  for (index = 0; index < input_count; index += 1) {
    status = bb_plan_analyze(builder, inputs[index]);
    if (status != BB_STATUS_OK) return status;
    if (builder->direct[inputs[index]]) continue;
    if (!builder->options->allow_cpu_fallback ||
        (builder->backend->capabilities.flags & BB_BACKEND_CAN_UPLOAD) == 0)
      return BB_STATUS_UNSUPPORTED;
    if (!builder->step_added[inputs[index]]) {
      if (builder->upload_count >= builder->backend->capabilities.max_resources)
        return BB_STATUS_LIMIT_EXCEEDED;
      status = bb_plan_add_step(
        builder,
        BB_RENDER_PLAN_CPU_BAKE_UPLOAD,
        inputs[index],
        BB_BACKEND_EQUIVALENCE_EXACT,
        0
      );
      if (status != BB_STATUS_OK) return status;
      builder->step_added[inputs[index]] = 1;
      builder->upload_count += 1;
    }
  }
  builder->direct[node] = 1;
  builder->state[node] = 2;
  bb_plan_accumulate_equivalence(
    builder->plan,
    equivalence,
    equivalence == BB_BACKEND_EQUIVALENCE_BOUNDED
      ? builder->backend->capabilities.bounded_max_channel_error
      : 0
  );
  return BB_STATUS_OK;
}

bb_status bb_render_plan_create(
  bb_context *context,
  const bb_image_graph *graph,
  bb_image_node root,
  const bb_backend_interface *backend,
  const bb_render_plan_options *options,
  bb_render_plan **out_plan
) {
  bb_render_plan_options defaults;
  bb_plan_builder builder;
  bb_render_plan *plan = NULL;
  const size_t node_count = bb_image_graph_node_count(graph);
  size_t step_bytes;
  size_t scratch_bytes;
  bb_status status;
  if (out_plan == NULL) return BB_STATUS_INVALID_ARGUMENT;
  *out_plan = NULL;
  if (context == NULL || graph == NULL || backend == NULL || root == BB_IMAGE_NODE_NONE || root > node_count ||
      backend->struct_size != sizeof(*backend) ||
      backend->capabilities.struct_size != sizeof(backend->capabilities)) return BB_STATUS_INVALID_ARGUMENT;
  if (options == NULL) {
    bb_render_plan_options_init(&defaults);
    options = &defaults;
  }
  if (options->struct_size != sizeof(*options)) return BB_STATUS_INVALID_ARGUMENT;
  if (node_count == SIZE_MAX || node_count + 1 > SIZE_MAX / 3) return BB_STATUS_OVERFLOW;
  scratch_bytes = (node_count + 1) * 3;
  if (node_count + 1 > SIZE_MAX / sizeof(*plan->steps)) return BB_STATUS_OVERFLOW;
  step_bytes = (node_count + 1) * sizeof(*plan->steps);
  status = bb_context_allocate(context, sizeof(*plan), _Alignof(bb_render_plan), (void **)&plan);
  if (status != BB_STATUS_OK) return status;
  memset(plan, 0, sizeof(*plan));
  plan->context = context;
  plan->equivalence = BB_BACKEND_EQUIVALENCE_EXACT;
  plan->step_capacity = node_count + 1;
  status = bb_context_allocate(context, step_bytes, _Alignof(bb_render_plan_step), (void **)&plan->steps);
  if (status != BB_STATUS_OK) goto fail;
  memset(plan->steps, 0, step_bytes);
  memset(&builder, 0, sizeof(builder));
  builder.context = context;
  builder.graph = graph;
  builder.backend = backend;
  builder.options = options;
  builder.plan = plan;
  builder.node_slots = node_count + 1;
  builder.scratch_bytes = scratch_bytes;
  status = bb_context_allocate(context, scratch_bytes, _Alignof(uint8_t), (void **)&builder.state);
  if (status != BB_STATUS_OK) goto fail;
  memset(builder.state, 0, scratch_bytes);
  builder.direct = builder.state + builder.node_slots;
  builder.step_added = builder.direct + builder.node_slots;
  status = bb_plan_analyze(&builder, root);
  if (status == BB_STATUS_OK) {
    if (builder.direct[root])
      status = bb_plan_add_step(
        &builder,
        BB_RENDER_PLAN_BACKEND_DIRECT,
        root,
        plan->equivalence,
        plan->max_channel_error
      );
    else if (!options->allow_cpu_fallback) status = BB_STATUS_UNSUPPORTED;
    else
      status = bb_plan_add_step(
        &builder,
        BB_RENDER_PLAN_CPU_FINAL,
        root,
        BB_BACKEND_EQUIVALENCE_EXACT,
        0
      );
  }
  bb_context_deallocate(context, builder.state, scratch_bytes, _Alignof(uint8_t));
  if (status != BB_STATUS_OK) goto fail;
  *out_plan = plan;
  return BB_STATUS_OK;

fail:
  if (plan != NULL) {
    bb_context_deallocate(context, plan->steps, step_bytes, _Alignof(bb_render_plan_step));
    bb_context_deallocate(context, plan, sizeof(*plan), _Alignof(bb_render_plan));
  }
  return status;
}

void bb_render_plan_destroy(bb_render_plan *plan) {
  size_t bytes;
  bb_context *context;
  if (plan == NULL) return;
  context = plan->context;
  bytes = plan->step_capacity * sizeof(*plan->steps);
  bb_context_deallocate(context, plan->steps, bytes, _Alignof(bb_render_plan_step));
  bb_context_deallocate(context, plan, sizeof(*plan), _Alignof(bb_render_plan));
}

size_t bb_render_plan_step_count(const bb_render_plan *plan) {
  return plan == NULL ? 0 : plan->step_count;
}

bb_status bb_render_plan_step_get(
  const bb_render_plan *plan,
  size_t index,
  bb_render_plan_step *out_step
) {
  if (plan == NULL || out_step == NULL) return BB_STATUS_INVALID_ARGUMENT;
  if (index >= plan->step_count) return BB_STATUS_NOT_FOUND;
  *out_step = plan->steps[index];
  return BB_STATUS_OK;
}

bb_backend_equivalence bb_render_plan_equivalence(const bb_render_plan *plan) {
  return plan == NULL ? BB_BACKEND_EQUIVALENCE_UNSUPPORTED : plan->equivalence;
}

uint32_t bb_render_plan_max_channel_error(const bb_render_plan *plan) {
  return plan == NULL ? 0 : plan->max_channel_error;
}

uint64_t bb_render_plan_baked_bytes(const bb_render_plan *plan) {
  return plan == NULL ? 0 : plan->baked_bytes;
}

bb_status bb_render_plan_execute_with_options(
  bb_context *context,
  const bb_image_graph *graph,
  const bb_render_plan *plan,
  const bb_backend_interface *backend,
  const bb_render_options *options,
  bb_graph_asset_decode_fn decode_asset,
  void *decode_user,
  bb_surface **out_surface
) {
  size_t index;
  uint64_t work_units = 0;
  if (out_surface == NULL) return BB_STATUS_INVALID_ARGUMENT;
  *out_surface = NULL;
  if (context == NULL || graph == NULL || plan == NULL || backend == NULL ||
      backend->struct_size != sizeof(*backend)) return BB_STATUS_INVALID_ARGUMENT;
  if (options != NULL && options->struct_size != sizeof(*options)) return BB_STATUS_INVALID_ARGUMENT;
  for (index = 0; index < plan->step_count; index += 1) {
    const bb_render_plan_step *step = &plan->steps[index];
    const uint64_t step_work = (uint64_t)step->width * (uint64_t)step->height;
    if (UINT64_MAX - work_units < step_work ||
        (options != NULL && work_units + step_work > options->max_work_units))
      return BB_STATUS_LIMIT_EXCEEDED;
    work_units += step_work;
  }
  for (index = 0; index < plan->step_count; index += 1) {
    const bb_render_plan_step *step = &plan->steps[index];
    bb_surface *surface = NULL;
    bb_status status;
    if (options != NULL && options->should_cancel != NULL && options->should_cancel(options->user))
      return BB_STATUS_CANCELLED;
    if (step->kind == BB_RENDER_PLAN_BACKEND_DIRECT) {
      if (backend->render_direct == NULL || (backend->capabilities.flags & BB_BACKEND_CAN_READBACK) == 0)
        return BB_STATUS_UNSUPPORTED;
      status = backend->render_direct(backend->user, graph, step->root, &surface);
      if (status != BB_STATUS_OK) return status;
      *out_surface = surface;
    } else {
      status = bb_image_graph_render_raster_with_options(
        context,
        graph,
        step->root,
        options,
        decode_asset,
        decode_user,
        &surface
      );
      if (status != BB_STATUS_OK) return status;
      if (step->kind == BB_RENDER_PLAN_CPU_FINAL) *out_surface = surface;
      else {
        if (backend->upload_baked == NULL) {
          bb_surface_destroy(surface);
          return BB_STATUS_UNSUPPORTED;
        }
        status = backend->upload_baked(backend->user, step->root, step->structural_hash, surface);
        bb_surface_destroy(surface);
        if (status != BB_STATUS_OK) return status;
      }
    }
  }
  return *out_surface == NULL ? BB_STATUS_INTERNAL_ERROR : BB_STATUS_OK;
}

bb_status bb_render_plan_execute(
  bb_context *context,
  const bb_image_graph *graph,
  const bb_render_plan *plan,
  const bb_backend_interface *backend,
  bb_graph_asset_decode_fn decode_asset,
  void *decode_user,
  bb_surface **out_surface
) {
  return bb_render_plan_execute_with_options(
    context,
    graph,
    plan,
    backend,
    NULL,
    decode_asset,
    decode_user,
    out_surface
  );
}
