#include "program_internal.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

void bb_program_destroy(bb_program *program) {
  bb_program_callback_state *callback;
  bb_context *context;
  size_t index;
  size_t bytes;
  if (program == NULL) return;
  context = program->context;
  for (index = 0; index < program->output_count; index += 1)
    bb_collection_release(program->outputs[index].artifacts);
  for (index = 0; index < program->binding_count; index += 1)
    bb_semantic_release(&program->bindings[index].value);
  for (index = 0; index < program->module_count; index += 1)
    bb_context_deallocate(
      context,
      program->modules[index].identity,
      program->modules[index].identity_bytes,
      _Alignof(char)
    );
  callback = program->callbacks;
  while (callback != NULL) {
    bb_program_callback_state *next = callback->next;
    bb_context_deallocate(context, callback, sizeof(*callback), _Alignof(bb_program_callback_state));
    callback = next;
  }
  bb_image_graph_destroy(program->graph);
  bb_context_deallocate(context, program->diagnostic_order, program->diagnostic_order_bytes, _Alignof(size_t));
  bb_diagnostic_store_destroy(&program->diagnostics);
  bytes = program->output_capacity * sizeof(*program->outputs);
  bb_context_deallocate(context, program->outputs, bytes, _Alignof(bb_program_output_record));
  bytes = program->trace_capacity * sizeof(*program->traces);
  bb_context_deallocate(context, program->traces, bytes, _Alignof(bb_semantic_trace));
  bytes = program->binding_capacity * sizeof(*program->bindings);
  bb_context_deallocate(context, program->bindings, bytes, _Alignof(bb_program_binding));
  bytes = program->module_capacity * sizeof(*program->modules);
  bb_context_deallocate(context, program->modules, bytes, _Alignof(bb_program_module_record));
  bb_context_deallocate(context, program, sizeof(*program), _Alignof(bb_program));
}

const bb_image_graph *bb_program_image_graph(const bb_program *program) {
  return program == NULL ? NULL : program->graph;
}

size_t bb_program_diagnostic_count(const bb_program *program) {
  return program == NULL ? 0 : bb_diagnostic_store_count(&program->diagnostics);
}

bb_status bb_program_diagnostic(const bb_program *program, size_t index, bb_diagnostic *out_diagnostic) {
  if (program == NULL || out_diagnostic == NULL) return BB_STATUS_INVALID_ARGUMENT;
  if (index >= bb_program_diagnostic_count(program)) return BB_STATUS_NOT_FOUND;
  return bb_diagnostic_store_get(&program->diagnostics, program->diagnostic_order[index], out_diagnostic);
}

size_t bb_program_trace_count(const bb_program *program) {
  return program == NULL ? 0 : program->trace_count;
}

bb_status bb_program_trace(const bb_program *program, size_t index, bb_semantic_trace *out_trace) {
  if (program == NULL || out_trace == NULL) return BB_STATUS_INVALID_ARGUMENT;
  if (index >= program->trace_count) return BB_STATUS_NOT_FOUND;
  *out_trace = program->traces[index];
  return BB_STATUS_OK;
}

bb_status bb_program_trace_at(
  const bb_program *program,
  bb_source_id source_id,
  uint32_t byte_offset,
  bb_semantic_trace *out_trace
) {
  size_t best = SIZE_MAX;
  uint32_t best_length = UINT32_MAX;
  size_t index;
  if (program == NULL || out_trace == NULL || source_id == BB_SOURCE_ID_NONE) return BB_STATUS_INVALID_ARGUMENT;
  for (index = 0; index < program->trace_count; index += 1) {
    const bb_semantic_trace *trace = &program->traces[index];
    const uint32_t length = trace->span.byte_end - trace->span.byte_start;
    if (trace->span.source_id == source_id && byte_offset >= trace->span.byte_start &&
        byte_offset <= trace->span.byte_end && length <= best_length) {
      best = index;
      best_length = length;
    }
  }
  if (best == SIZE_MAX) return BB_STATUS_NOT_FOUND;
  *out_trace = program->traces[best];
  return BB_STATUS_OK;
}

size_t bb_program_output_count(const bb_program *program) {
  return program == NULL ? 0 : program->output_count;
}

size_t bb_program_parameter_count(const bb_program *program) {
  size_t count = 0;
  size_t index;
  if (program == NULL) return 0;
  for (index = 0; index < program->binding_count; index += 1)
    if (!program->bindings[index].duplicate && bb_program_value_is_parameter(&program->bindings[index].value)) count += 1;
  return count;
}

bb_status bb_program_parameter(
  const bb_program *program,
  size_t parameter_index,
  bb_program_parameter_info *out_info
) {
  size_t found = 0;
  size_t index;
  if (program == NULL || out_info == NULL) return BB_STATUS_INVALID_ARGUMENT;
  for (index = 0; index < program->binding_count; index += 1) {
    const bb_program_binding *binding = &program->bindings[index];
    if (binding->duplicate || !bb_program_value_is_parameter(&binding->value)) continue;
    if (found == parameter_index) {
      memset(out_info, 0, sizeof(*out_info));
      out_info->name = binding->name;
      out_info->span = binding->span;
      out_info->type = binding->value.type;
      if (binding->value.type == BB_SEMANTIC_BOOL)
        out_info->value.boolean = binding->value.data.boolean;
      else if (binding->value.type == BB_SEMANTIC_INTEGER)
        out_info->value.integer = binding->value.data.integer;
      else if (binding->value.type == BB_SEMANTIC_NUMBER || binding->value.type == BB_SEMANTIC_DEGREES ||
               binding->value.type == BB_SEMANTIC_PERCENTAGE)
        out_info->value.number = binding->value.data.number;
      else if (binding->value.type == BB_SEMANTIC_COLOR)
        out_info->value.color = binding->value.data.color;
      return BB_STATUS_OK;
    }
    found += 1;
  }
  return BB_STATUS_NOT_FOUND;
}

bb_status bb_program_output(const bb_program *program, size_t output_index, bb_program_output_info *out_info) {
  if (program == NULL || out_info == NULL) return BB_STATUS_INVALID_ARGUMENT;
  if (output_index >= program->output_count) return BB_STATUS_NOT_FOUND;
  *out_info = program->outputs[output_index].info;
  return BB_STATUS_OK;
}

bb_status bb_program_output_artifact(
  const bb_program *program,
  size_t output_index,
  uint64_t item_index,
  bb_artifact_value *out_artifact
) {
  bb_value value;
  bb_status status;
  if (program == NULL || out_artifact == NULL) return BB_STATUS_INVALID_ARGUMENT;
  if (output_index >= program->output_count) return BB_STATUS_NOT_FOUND;
  status = bb_collection_get(program->outputs[output_index].artifacts, item_index, &value, 1);
  if (status != BB_STATUS_OK) return status;
  if (value.kind != BB_VALUE_ARTIFACT) return BB_STATUS_INTERNAL_ERROR;
  *out_artifact = value.data.artifact;
  return BB_STATUS_OK;
}

typedef struct bb_program_render_state {
  bb_program *program;
  const bb_render_options *options;
} bb_program_render_state;

static bb_status bb_program_artifact_renderer(
  void *user,
  bb_context *context,
  const bb_image_graph *graph,
  const bb_artifact_value *artifact,
  bb_surface **out_surface
) {
  bb_program_render_state *state = user;
  return bb_image_graph_render_raster_with_options(
    context,
    graph,
    artifact->image,
    state->options,
    state->program->options.decode_asset,
    state->program->options.user,
    out_surface
  );
}

bb_status bb_program_render_output_with_options(
  bb_program *program,
  size_t output_index,
  uint64_t item_index,
  const bb_render_options *options,
  bb_surface **out_surface
) {
  bb_program_render_state state;
  if (out_surface == NULL) return BB_STATUS_INVALID_ARGUMENT;
  *out_surface = NULL;
  if (program == NULL) return BB_STATUS_INVALID_ARGUMENT;
  if (options != NULL && options->struct_size != sizeof(*options)) return BB_STATUS_INVALID_ARGUMENT;
  if (options != NULL && options->should_cancel != NULL && options->should_cancel(options->user))
    return BB_STATUS_CANCELLED;
  if (output_index >= program->output_count) return BB_STATUS_NOT_FOUND;
  state.program = program;
  state.options = options;
  return bb_collection_render_artifact_with(
    program->context,
    program->outputs[output_index].artifacts,
    item_index,
    program->graph,
    bb_program_artifact_renderer,
    &state,
    out_surface
  );
}

bb_status bb_program_render_output(
  bb_program *program,
  size_t output_index,
  uint64_t item_index,
  bb_surface **out_surface
) {
  return bb_program_render_output_with_options(program, output_index, item_index, NULL, out_surface);
}

bb_status bb_program_output_encoded(
  const bb_program *program,
  size_t output_index,
  uint64_t item_index,
  bb_bytes *out_bytes
) {
  bb_artifact_value artifact;
  bb_status status;
  if (out_bytes == NULL) return BB_STATUS_INVALID_ARGUMENT;
  *out_bytes = (bb_bytes){NULL, 0};
  if (program == NULL || program->options.encoded_asset == NULL) return BB_STATUS_UNSUPPORTED;
  status = bb_program_output_artifact(program, output_index, item_index, &artifact);
  if (status != BB_STATUS_OK) return status;
  if (artifact.alias_identity != BB_ALIAS_BYTES || artifact.alias_target.length == 0) return BB_STATUS_UNSUPPORTED;
  return program->options.encoded_asset(program->options.user, artifact.alias_target, out_bytes);
}
