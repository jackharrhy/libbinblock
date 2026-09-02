#include "program_internal.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static int bb_program_unquote_is(bb_string_view string, const char *expected) {
  bb_string_view unquoted;
  return bb_program_unquote(string, &unquoted) && bb_string_view_is(unquoted, expected);
}

static void bb_program_copy_syntax_diagnostics(bb_program *program, const bb_syntax_tree *syntax) {
  size_t index;
  for (index = 0; index < bb_syntax_tree_diagnostic_count(syntax) && program->status == BB_STATUS_OK; index += 1) {
    bb_diagnostic diagnostic;
    bb_status status = bb_syntax_tree_diagnostic(syntax, index, &diagnostic);
    if (status == BB_STATUS_OK)
      status = bb_diagnostic_store_push(
        &program->diagnostics,
        diagnostic.severity,
        diagnostic.code,
        diagnostic.message,
        diagnostic.primary_span,
        diagnostic.related_spans,
        diagnostic.related_span_count
      );
    bb_program_fail(program, status);
  }
}

static size_t bb_program_find_module_record(bb_program *program, bb_string_view identity) {
  size_t index;
  for (index = 0; index < program->module_count; index += 1) {
    const bb_program_module_record *record = &program->modules[index];
    if (record->identity_bytes == identity.length &&
        memcmp(record->identity, identity.data, identity.length) == 0) return index;
  }
  return SIZE_MAX;
}

static void bb_program_resolve_module_specifier(
  bb_program *program,
  bb_string_view specifier,
  bb_string_view importer_identity,
  bb_span span,
  uint32_t depth
) {
  bb_module_request request;
  bb_resolved_module resolved;
  bb_program_module_record *record;
  bb_string_view owned_identity;
  size_t record_index;
  bb_status status;
  if (program->status != BB_STATUS_OK) return;
  if (depth > program->limits.max_import_depth) {
    bb_program_diagnostic_span(
      program,
      BB_SEMANTIC_DIAGNOSTIC_IMPORT_DEPTH,
      "Module import depth limit exceeded.",
      span
    );
    return;
  }
  if (program->options.resolve_module == NULL) {
    bb_program_diagnostic_span(
      program,
      BB_SEMANTIC_DIAGNOSTIC_UNKNOWN_IMPORT,
      "No host module resolver accepted this import.",
      span
    );
    return;
  }
  request.specifier = specifier;
  request.importer_identity = importer_identity;
  memset(&resolved, 0, sizeof(resolved));
  status = program->options.resolve_module(program->options.user, &request, &resolved);
  if (status != BB_STATUS_OK || resolved.identity.length == 0 || resolved.identity.data == NULL ||
      (resolved.source.length != 0 && resolved.source.data == NULL) ||
      (resolved.source_name.length != 0 && resolved.source_name.data == NULL)) {
    bb_program_diagnostic_span(
      program,
      BB_SEMANTIC_DIAGNOSTIC_UNKNOWN_IMPORT,
      "The host could not resolve this module.",
      span
    );
    return;
  }
  record_index = bb_program_find_module_record(program, resolved.identity);
  if (record_index != SIZE_MAX) {
    if (program->modules[record_index].state == 1)
      bb_program_diagnostic_span(
        program,
        BB_SEMANTIC_DIAGNOSTIC_MODULE_CYCLE,
        "Module import cycle detected.",
        span
      );
    return;
  }
  status = bb_program_grow(
    program,
    (void **)&program->modules,
    &program->module_capacity,
    sizeof(*program->modules),
    _Alignof(bb_program_module_record),
    program->module_count + 1
  );
  if (status != BB_STATUS_OK) {
    bb_program_fail(program, status);
    return;
  }
  record_index = program->module_count;
  record = &program->modules[record_index];
  memset(record, 0, sizeof(*record));
  record->identity_bytes = resolved.identity.length;
  status = bb_context_allocate(
    program->context,
    record->identity_bytes,
    _Alignof(char),
    (void **)&record->identity
  );
  if (status != BB_STATUS_OK) {
    bb_program_fail(program, status);
    return;
  }
  memcpy(record->identity, resolved.identity.data, record->identity_bytes);
  record->state = 1;
  program->module_count += 1;
  owned_identity = (bb_string_view){record->identity, record->identity_bytes};
  {
    bb_bytes module_source = resolved.source;
    bb_string_view source_name = resolved.source_name.length == 0 ? owned_identity : resolved.source_name;
    bb_source_id source_id = BB_SOURCE_ID_NONE;
    bb_syntax_tree *syntax = NULL;
    bb_syntax_node root;
    bb_syntax_node_info root_info;
    size_t index;
    status = bb_context_add_source(program->context, source_name, module_source, &source_id);
    if (status == BB_STATUS_OK) status = bb_syntax_parse(program->context, source_id, &syntax);
    if (status != BB_STATUS_OK) bb_program_fail(program, status);
    if (syntax != NULL) {
      bb_program_copy_syntax_diagnostics(program, syntax);
      root = bb_syntax_tree_root(syntax);
      if (bb_program_node_info(syntax, root, &root_info) == BB_STATUS_OK) {
        for (index = 0; index < root_info.child_count && program->status == BB_STATUS_OK; index += 1) {
          const bb_syntax_node statement = bb_program_child(syntax, root, index);
          bb_syntax_node_info statement_info;
          (void)bb_program_node_info(syntax, statement, &statement_info);
          if (statement_info.kind == BB_SYNTAX_IMPORT_STATEMENT) {
            bb_string_view nested;
            const bb_string_view raw = bb_program_node_text(
              syntax,
              bb_program_child(syntax, statement, 0)
            );
            if (!bb_program_unquote(raw, &nested)) continue;
            if (bb_string_view_is(nested, "binblock/basic")) program->has_basic_module = 1;
            else if (bb_string_view_is(nested, "binblock/reference-set")) program->has_reference_module = 1;
            else
              bb_program_resolve_module_specifier(
                program,
                nested,
                owned_identity,
                statement_info.span,
                depth + 1
              );
          }
        }
      }
      bb_syntax_tree_destroy(syntax);
    }
  }
  program->modules[record_index].state = 2;
}

static void bb_program_collect_import(
  bb_program *program,
  const bb_syntax_tree *syntax,
  bb_syntax_node statement,
  bb_syntax_node_info info
) {
  const bb_syntax_node path = bb_program_child(syntax, statement, 0);
  const bb_string_view text = bb_program_node_text(syntax, path);
  bb_string_view specifier;
  if (bb_program_unquote_is(text, "binblock/basic")) program->has_basic_module = 1;
  else if (bb_program_unquote_is(text, "binblock/reference-set")) {
    program->has_reference_module = 1;
  } else if (bb_program_unquote(text, &specifier))
    bb_program_resolve_module_specifier(program, specifier, (bb_string_view){NULL, 0}, info.span, 1);
}

static void bb_program_collect_binding(
  bb_program *program,
  const bb_syntax_tree *syntax,
  bb_syntax_node statement,
  bb_syntax_node_info info
) {
  bb_program_binding *binding;
  const bb_syntax_node name_node = bb_program_child(syntax, statement, 0);
  const bb_string_view name = bb_program_node_text(syntax, name_node);
  size_t index;
  bb_status status = bb_program_grow(
    program,
    (void **)&program->bindings,
    &program->binding_capacity,
    sizeof(*program->bindings),
    _Alignof(bb_program_binding),
    program->binding_count + 1
  );
  if (status != BB_STATUS_OK) {
    bb_program_fail(program, status);
    return;
  }
  binding = &program->bindings[program->binding_count];
  memset(binding, 0, sizeof(*binding));
  binding->name = name;
  binding->span = info.span;
  binding->expression = bb_program_child(syntax, statement, 1);
  for (index = 0; index < program->binding_count; index += 1) {
    if (bb_string_view_equal(program->bindings[index].name, name)) {
      binding->duplicate = 1;
      bb_program_diagnostic_span(
        program,
        BB_SEMANTIC_DIAGNOSTIC_DUPLICATE_BINDING,
        "Duplicate immutable binding.",
        info.span
      );
      break;
    }
  }
  program->binding_count += 1;
}

static void bb_program_mark_output_trace(bb_program *program, bb_span span, size_t output_index) {
  size_t index;
  for (index = 0; index < program->trace_count; index += 1) {
    bb_semantic_trace *trace = &program->traces[index];
    if (trace->span.source_id == span.source_id && trace->span.byte_start == span.byte_start &&
        trace->span.byte_end == span.byte_end) {
      trace->output_index = output_index;
      return;
    }
  }
}

static void bb_program_add_output(
  bb_program *program,
  bb_semantic_value value,
  bb_span span
) {
  bb_collection *artifacts = NULL;
  bb_program_output_record *output;
  uint64_t cardinality;
  bb_status status;
  if (value.type == BB_SEMANTIC_IMAGE) {
    char key[32];
    bb_value artifact;
    const int length = snprintf(key, sizeof(key), "output-%lu", (unsigned long)program->output_count);
    memset(&artifact, 0, sizeof(artifact));
    artifact.kind = BB_VALUE_ARTIFACT;
    artifact.data.artifact.key = value.data.image.key.length == 0
                                   ? (bb_string_view){key, (size_t)length}
                                   : value.data.image.key;
    artifact.data.artifact.path = value.data.image.path.length == 0 ? artifact.data.artifact.key
                                                                    : value.data.image.path;
    artifact.data.artifact.image = value.data.image.node;
    artifact.data.artifact.alias_identity = BB_ALIAS_NONE;
    artifact.data.artifact.provenance = span;
    if (value.data.image.encoded_alias && program->options.encoded_asset != NULL) {
      bb_graph_asset_desc asset_desc;
      if (bb_image_graph_asset(program->graph, value.data.image.node, &asset_desc) == BB_STATUS_OK) {
        artifact.data.artifact.alias_identity = BB_ALIAS_BYTES;
        artifact.data.artifact.alias_target = asset_desc.content_id;
      }
    }
    status = bb_collection_from_values(program->context, &artifact, 1, &artifacts);
    if (status != BB_STATUS_OK) {
      bb_program_fail(program, status);
      return;
    }
  } else if (value.type == BB_SEMANTIC_COLLECTION &&
             value.data.collection.shape == BB_SEMANTIC_COLLECTION_ARTIFACTS) {
    artifacts = bb_collection_retain(value.data.collection.plan);
    if (artifacts == NULL) {
      bb_program_fail(program, BB_STATUS_LIMIT_EXCEEDED);
      return;
    }
  } else {
    bb_program_diagnostic_span(
      program,
      BB_SEMANTIC_DIAGNOSTIC_INVALID_OUTPUT,
      "A standalone output must be an image or image collection.",
      span
    );
    return;
  }
  status = bb_collection_count(artifacts, &cardinality);
  if (status != BB_STATUS_OK || cardinality > program->limits.max_output_count) {
    bb_collection_release(artifacts);
    bb_program_diagnostic_span(
      program,
      BB_SEMANTIC_DIAGNOSTIC_CARDINALITY,
      "Output cardinality is invalid or exceeds the configured output limit.",
      span
    );
    return;
  }
  status = bb_program_grow(
    program,
    (void **)&program->outputs,
    &program->output_capacity,
    sizeof(*program->outputs),
    _Alignof(bb_program_output_record),
    program->output_count + 1
  );
  if (status != BB_STATUS_OK) {
    bb_collection_release(artifacts);
    bb_program_fail(program, status);
    return;
  }
  output = &program->outputs[program->output_count];
  output->artifacts = artifacts;
  output->info.span = span;
  output->info.item_type = BB_SEMANTIC_ARTIFACT;
  output->info.cardinality = cardinality;
  bb_program_mark_output_trace(program, span, program->output_count);
  program->output_count += 1;
}

static bb_status bb_program_build_diagnostic_order(bb_program *program) {
  const size_t count = bb_diagnostic_store_count(&program->diagnostics);
  size_t bytes;
  size_t index;
  bb_status status;
  if (!bb_size_multiply(count, sizeof(*program->diagnostic_order), &bytes)) return BB_STATUS_OVERFLOW;
  program->diagnostic_order_bytes = bytes;
  status = bb_context_allocate(
    program->context,
    bytes,
    _Alignof(size_t),
    (void **)&program->diagnostic_order
  );
  if (status != BB_STATUS_OK) return status;
  for (index = 0; index < count; index += 1) {
    size_t position = index;
    bb_diagnostic current;
    program->diagnostic_order[index] = index;
    status = bb_diagnostic_store_get(&program->diagnostics, index, &current);
    if (status != BB_STATUS_OK) return status;
    while (position > 0) {
      bb_diagnostic previous;
      status = bb_diagnostic_store_get(&program->diagnostics, program->diagnostic_order[position - 1], &previous);
      if (status != BB_STATUS_OK) return status;
      if (previous.primary_span.source_id < current.primary_span.source_id ||
          (previous.primary_span.source_id == current.primary_span.source_id &&
           (previous.primary_span.byte_start < current.primary_span.byte_start ||
            (previous.primary_span.byte_start == current.primary_span.byte_start &&
             previous.primary_span.byte_end <= current.primary_span.byte_end)))) break;
      program->diagnostic_order[position] = program->diagnostic_order[position - 1];
      position -= 1;
    }
    program->diagnostic_order[position] = index;
  }
  return BB_STATUS_OK;
}

void bb_compile_options_init(bb_compile_options *options) {
  if (options == NULL) return;
  memset(options, 0, sizeof(*options));
  options->struct_size = (uint32_t)sizeof(*options);
}

bb_status bb_program_compile_with_options(
  bb_context *context,
  const bb_syntax_tree *syntax,
  const bb_compile_options *options,
  bb_program **out_program
) {
  bb_compile_options defaults;
  const bb_compile_options *effective_options = options;
  bb_program *program = NULL;
  bb_syntax_node root;
  bb_syntax_node_info root_info;
  size_t index;
  bb_status status;
  if (out_program == NULL) return BB_STATUS_INVALID_ARGUMENT;
  *out_program = NULL;
  if (context == NULL || syntax == NULL) return BB_STATUS_INVALID_ARGUMENT;
  if (effective_options == NULL) {
    bb_compile_options_init(&defaults);
    effective_options = &defaults;
  }
  if (effective_options->struct_size != sizeof(*effective_options)) return BB_STATUS_INVALID_ARGUMENT;
  if (effective_options->parameter_override_count != 0 && effective_options->parameter_overrides == NULL)
    return BB_STATUS_INVALID_ARGUMENT;
  for (index = 0; index < effective_options->parameter_override_count; index += 1) {
    const bb_parameter_override *override = &effective_options->parameter_overrides[index];
    size_t previous;
    if (override->name.length == 0 || override->name.data == NULL ||
        (override->type != BB_SEMANTIC_BOOL && override->type != BB_SEMANTIC_INTEGER &&
         override->type != BB_SEMANTIC_NUMBER && override->type != BB_SEMANTIC_DEGREES &&
         override->type != BB_SEMANTIC_PERCENTAGE && override->type != BB_SEMANTIC_COLOR))
      return BB_STATUS_INVALID_ARGUMENT;
    for (previous = 0; previous < index; previous += 1)
      if (bb_string_view_equal(override->name, effective_options->parameter_overrides[previous].name))
        return BB_STATUS_INVALID_ARGUMENT;
  }
  status = bb_context_allocate(context, sizeof(*program), _Alignof(bb_program), (void **)&program);
  if (status != BB_STATUS_OK) return status;
  memset(program, 0, sizeof(*program));
  program->context = context;
  program->options = *effective_options;
  program->status = BB_STATUS_OK;
  status = bb_context_get_limits(context, &program->limits);
  if (status != BB_STATUS_OK) goto fail;
  status = bb_diagnostic_store_init(&program->diagnostics, context);
  if (status != BB_STATUS_OK) goto fail;
  status = bb_image_graph_create(context, &program->graph);
  if (status != BB_STATUS_OK) goto fail;
  bb_program_copy_syntax_diagnostics(program, syntax);
  if (program->status != BB_STATUS_OK) {
    status = program->status;
    goto fail;
  }
  root = bb_syntax_tree_root(syntax);
  status = bb_program_node_info(syntax, root, &root_info);
  if (status != BB_STATUS_OK || root_info.kind != BB_SYNTAX_PROGRAM) {
    status = BB_STATUS_INVALID_ARGUMENT;
    goto fail;
  }
  for (index = 0; index < root_info.child_count && program->status == BB_STATUS_OK; index += 1) {
    const bb_syntax_node statement = bb_program_child(syntax, root, index);
    bb_syntax_node_info info;
    (void)bb_program_node_info(syntax, statement, &info);
    if (info.kind == BB_SYNTAX_IMPORT_STATEMENT) bb_program_collect_import(program, syntax, statement, info);
    else if (info.kind == BB_SYNTAX_BINDING_STATEMENT) bb_program_collect_binding(program, syntax, statement, info);
  }
  for (index = 0; index < program->binding_count && program->status == BB_STATUS_OK; index += 1) {
    bb_semantic_value value;
    if (program->bindings[index].duplicate) continue;
    value = bb_program_evaluate_binding(program, syntax, &program->bindings[index], program->bindings[index].span);
    bb_semantic_release(&value);
  }
  for (index = 0; index < root_info.child_count && program->status == BB_STATUS_OK; index += 1) {
    const bb_syntax_node statement = bb_program_child(syntax, root, index);
    bb_syntax_node_info info;
    (void)bb_program_node_info(syntax, statement, &info);
    if (info.kind == BB_SYNTAX_EXPRESSION_STATEMENT) {
      const bb_syntax_node expression = bb_program_child(syntax, statement, 0);
      bb_syntax_node_info expression_info;
      bb_semantic_value value;
      (void)bb_program_node_info(syntax, expression, &expression_info);
      value = bb_program_evaluate(program, syntax, expression);
      bb_program_add_output(program, value, expression_info.span);
      bb_semantic_release(&value);
    }
  }
  if (program->status != BB_STATUS_OK) {
    status = program->status;
    goto fail;
  }
  /* Overrides are a compile-time borrowed view. Resolvers and their user data
   * remain available for lazy asset decode, but override storage must not be
   * retained by the compiled program. */
  program->options.parameter_overrides = NULL;
  program->options.parameter_override_count = 0;
  status = bb_program_build_diagnostic_order(program);
  if (status != BB_STATUS_OK) goto fail;
  *out_program = program;
  return BB_STATUS_OK;

fail:
  bb_program_destroy(program);
  return status;
}

bb_status bb_program_compile(bb_context *context, const bb_syntax_tree *syntax, bb_program **out_program) {
  return bb_program_compile_with_options(context, syntax, NULL, out_program);
}
