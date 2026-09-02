#include "program_internal.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

typedef struct bb_program_asset_stack {
  const struct bb_program_asset_stack *parent;
  bb_string_view logical_id;
} bb_program_asset_stack;

typedef struct bb_program_owned_asset {
  char *content_id;
  size_t content_id_bytes;
  uint32_t width;
  uint32_t height;
  uint32_t has_encoded_bytes;
} bb_program_owned_asset;

static void bb_program_owned_asset_destroy(bb_program *program, bb_program_owned_asset *asset) {
  if (asset == NULL) return;
  bb_context_deallocate(
    program->context,
    asset->content_id,
    asset->content_id_bytes,
    _Alignof(char)
  );
  memset(asset, 0, sizeof(*asset));
}

static int bb_program_asset_stack_contains(
  const bb_program_asset_stack *stack,
  bb_string_view logical_id
) {
  while (stack != NULL) {
    if (bb_string_view_equal(stack->logical_id, logical_id)) return 1;
    stack = stack->parent;
  }
  return 0;
}

static int bb_program_resolve_asset_recursive(
  bb_program *program,
  const bb_asset_request *request,
  bb_span span,
  const bb_program_asset_stack *parent,
  uint32_t depth,
  bb_program_owned_asset *out_asset
) {
  bb_resolved_asset resolved;
  bb_program_asset_stack stack;
  bb_string_view *dependency_copies = NULL;
  size_t dependency_bytes = 0;
  bb_status status;
  size_t index;
  memset(out_asset, 0, sizeof(*out_asset));
  if (program->options.resolve_asset == NULL) {
    bb_program_diagnostic_span(
      program,
      BB_SEMANTIC_DIAGNOSTIC_ASSET_RESOLUTION,
      "No host asset resolver is configured.",
      span
    );
    return 0;
  }
  if (depth > program->limits.max_import_depth) {
    bb_program_diagnostic_span(
      program,
      BB_SEMANTIC_DIAGNOSTIC_ASSET_CYCLE,
      "Asset dependency depth limit exceeded.",
      span
    );
    return 0;
  }
  if (bb_program_asset_stack_contains(parent, request->logical_id)) {
    bb_program_diagnostic_span(
      program,
      BB_SEMANTIC_DIAGNOSTIC_ASSET_CYCLE,
      "Asset dependency cycle detected.",
      span
    );
    return 0;
  }
  memset(&resolved, 0, sizeof(resolved));
  status = program->options.resolve_asset(program->options.user, request, &resolved);
  if (status != BB_STATUS_OK || resolved.content_id.length == 0 || resolved.content_id.data == NULL ||
      resolved.width == 0 || resolved.height == 0 ||
      (resolved.dependency_count != 0 && resolved.dependencies == NULL)) {
    bb_program_diagnostic_span(
      program,
      BB_SEMANTIC_DIAGNOSTIC_ASSET_RESOLUTION,
      "The host could not resolve this logical asset.",
      span
    );
    return 0;
  }
  if ((request->expected_content_id.length != 0 &&
       !bb_string_view_equal(request->expected_content_id, resolved.content_id)) ||
      (request->has_expected_dimensions &&
       (request->expected_width != resolved.width || request->expected_height != resolved.height))) {
    bb_program_diagnostic_span(
      program,
      BB_SEMANTIC_DIAGNOSTIC_ASSET_CONSTRAINT,
      "Resolved asset does not satisfy its declared content or dimension constraints.",
      span
    );
    return 0;
  }
  out_asset->content_id_bytes = resolved.content_id.length;
  out_asset->width = resolved.width;
  out_asset->height = resolved.height;
  out_asset->has_encoded_bytes = !!resolved.has_encoded_bytes;
  status = bb_context_allocate(
    program->context,
    out_asset->content_id_bytes,
    _Alignof(char),
    (void **)&out_asset->content_id
  );
  if (status != BB_STATUS_OK) {
    bb_program_fail(program, status);
    return 0;
  }
  memcpy(out_asset->content_id, resolved.content_id.data, out_asset->content_id_bytes);
  if (resolved.dependency_count != 0) {
    if (!bb_size_multiply(resolved.dependency_count, sizeof(*dependency_copies), &dependency_bytes)) {
      bb_program_fail(program, BB_STATUS_OVERFLOW);
      bb_program_owned_asset_destroy(program, out_asset);
      return 0;
    }
    status = bb_context_allocate(
      program->context,
      dependency_bytes,
      _Alignof(bb_string_view),
      (void **)&dependency_copies
    );
    if (status != BB_STATUS_OK) {
      bb_program_fail(program, status);
      bb_program_owned_asset_destroy(program, out_asset);
      return 0;
    }
    memset(dependency_copies, 0, dependency_bytes);
    for (index = 0; index < resolved.dependency_count; index += 1) {
      const bb_string_view logical = resolved.dependencies[index];
      char *logical_copy = NULL;
      if (logical.length == 0 || logical.data == NULL) {
        bb_program_diagnostic_span(
          program,
          BB_SEMANTIC_DIAGNOSTIC_ASSET_RESOLUTION,
          "Asset dependency has an invalid logical identifier.",
          span
        );
        break;
      }
      status = bb_context_allocate(
        program->context,
        logical.length,
        _Alignof(char),
        (void **)&logical_copy
      );
      if (status != BB_STATUS_OK) {
        bb_program_fail(program, status);
        break;
      }
      memcpy(logical_copy, logical.data, logical.length);
      dependency_copies[index] = (bb_string_view){logical_copy, logical.length};
    }
    if (index != resolved.dependency_count) {
      size_t cleanup_index;
      for (cleanup_index = 0; cleanup_index < index; cleanup_index += 1)
        bb_context_deallocate(
          program->context,
          (void *)dependency_copies[cleanup_index].data,
          dependency_copies[cleanup_index].length,
          _Alignof(char)
        );
      bb_context_deallocate(program->context, dependency_copies, dependency_bytes, _Alignof(bb_string_view));
      bb_program_owned_asset_destroy(program, out_asset);
      return 0;
    }
  }
  stack.parent = parent;
  stack.logical_id = request->logical_id;
  for (index = 0; index < resolved.dependency_count; index += 1) {
    bb_asset_request dependency_request;
    bb_program_owned_asset dependency;
    memset(&dependency_request, 0, sizeof(dependency_request));
    dependency_request.logical_id = dependency_copies[index];
    if (!bb_program_resolve_asset_recursive(
          program,
          &dependency_request,
          span,
          &stack,
          depth + 1,
          &dependency
        )) {
      break;
    }
    bb_program_owned_asset_destroy(program, &dependency);
  }
  for (size_t cleanup_index = 0; cleanup_index < resolved.dependency_count; cleanup_index += 1)
    bb_context_deallocate(
      program->context,
      (void *)dependency_copies[cleanup_index].data,
      dependency_copies[cleanup_index].length,
      _Alignof(char)
    );
  if (dependency_copies != NULL)
    bb_context_deallocate(program->context, dependency_copies, dependency_bytes, _Alignof(bb_string_view));
  if (index != resolved.dependency_count) {
    bb_program_owned_asset_destroy(program, out_asset);
    return 0;
  }
  return 1;
}

bb_semantic_value bb_program_call_asset(
  bb_program *program,
  const bb_syntax_tree *syntax,
  bb_syntax_node call,
  bb_syntax_node_info call_info
) {
  bb_semantic_value logical_value = bb_semantic_error();
  bb_semantic_value result = bb_semantic_error();
  bb_asset_request request;
  bb_program_owned_asset resolved;
  bb_graph_asset_desc desc;
  bb_string_view logical_id;
  size_t index;
  bb_status status;
  if (call_info.child_count < 2) {
    bb_program_diagnostic_span(
      program,
      BB_SEMANTIC_DIAGNOSTIC_ARGUMENT_COUNT,
      "asset() expects a logical asset string.",
      call_info.span
    );
    return result;
  }
  logical_value = bb_program_evaluate(program, syntax, bb_program_child(syntax, call, 1));
  if (logical_value.type != BB_SEMANTIC_STRING || !bb_program_unquote(logical_value.data.string, &logical_id)) {
    bb_program_diagnostic_span(
      program,
      BB_SEMANTIC_DIAGNOSTIC_TYPE_MISMATCH,
      "asset() expects a string logical identifier.",
      call_info.span
    );
    bb_semantic_release(&logical_value);
    return result;
  }
  memset(&request, 0, sizeof(request));
  request.logical_id = logical_id;
  for (index = 2; index < call_info.child_count; index += 1) {
    const bb_syntax_node argument = bb_program_child(syntax, call, index);
    bb_syntax_node_info argument_info;
    bb_string_view name;
    bb_semantic_value value;
    (void)bb_program_node_info(syntax, argument, &argument_info);
    if (argument_info.kind != BB_SYNTAX_NAMED_ARGUMENT) {
      bb_program_diagnostic_span(
        program,
        BB_SEMANTIC_DIAGNOSTIC_ARGUMENT_COUNT,
        "Additional asset() constraints must be named.",
        argument_info.span
      );
      goto cleanup;
    }
    name = bb_program_node_text(syntax, bb_program_child(syntax, argument, 0));
    value = bb_program_evaluate(program, syntax, bb_program_child(syntax, argument, 1));
    if (bb_string_view_is(name, "hash") && value.type == BB_SEMANTIC_STRING) {
      if (!bb_program_unquote(value.data.string, &request.expected_content_id)) {
        bb_semantic_release(&value);
        goto constraint_error;
      }
    } else if (bb_string_view_is(name, "width") && value.type == BB_SEMANTIC_INTEGER &&
               value.data.integer > 0 && value.data.integer <= UINT32_MAX) {
      request.expected_width = (uint32_t)value.data.integer;
    } else if (bb_string_view_is(name, "height") && value.type == BB_SEMANTIC_INTEGER &&
               value.data.integer > 0 && value.data.integer <= UINT32_MAX) {
      request.expected_height = (uint32_t)value.data.integer;
    } else {
      bb_semantic_release(&value);
      goto constraint_error;
    }
    bb_semantic_release(&value);
  }
  if ((request.expected_width == 0) != (request.expected_height == 0)) goto constraint_error;
  request.has_expected_dimensions = request.expected_width != 0;
  if (!bb_program_resolve_asset_recursive(program, &request, call_info.span, NULL, 1, &resolved)) goto cleanup;
  desc.content_id = (bb_string_view){resolved.content_id, resolved.content_id_bytes};
  desc.width = resolved.width;
  desc.height = resolved.height;
  result.type = BB_SEMANTIC_IMAGE;
  result.data.image.width = resolved.width;
  result.data.image.height = resolved.height;
  result.data.image.encoded_alias = resolved.has_encoded_bytes;
  result.data.image.key = logical_id;
  result.data.image.path = logical_id;
  status = bb_image_graph_add_asset(program->graph, &desc, &result.data.image.node);
  if (status == BB_STATUS_OK) status = bb_image_graph_attach_span(program->graph, result.data.image.node, call_info.span);
  if (status != BB_STATUS_OK) {
    bb_program_fail(program, status);
    result = bb_semantic_error();
  }
  bb_program_owned_asset_destroy(program, &resolved);
  goto cleanup;

constraint_error:
  bb_program_diagnostic_span(
    program,
    BB_SEMANTIC_DIAGNOSTIC_ASSET_CONSTRAINT,
    "asset() constraints are hash: string and paired positive width:/height: integers.",
    call_info.span
  );

cleanup:
  bb_semantic_release(&logical_value);
  return result;
}

bb_semantic_value bb_program_call_fill(
  bb_program *program,
  const bb_syntax_tree *syntax,
  bb_syntax_node call,
  bb_syntax_node_info call_info
) {
  bb_semantic_value color = bb_semantic_error();
  bb_semantic_value result = bb_semantic_error();
  bb_status status;
  if (call_info.child_count != 2) {
    bb_program_diagnostic_span(
      program,
      BB_SEMANTIC_DIAGNOSTIC_ARGUMENT_COUNT,
      "fill() expects one color.",
      call_info.span
    );
    return result;
  }
  color = bb_program_evaluate(program, syntax, bb_program_child(syntax, call, 1));
  if (color.type != BB_SEMANTIC_COLOR) {
    bb_program_diagnostic_span(
      program,
      BB_SEMANTIC_DIAGNOSTIC_TYPE_MISMATCH,
      "fill() expects a color.",
      call_info.span
    );
    goto cleanup;
  }
  result.type = BB_SEMANTIC_IMAGE;
  result.data.image.width = 1;
  result.data.image.height = 1;
  status = bb_image_graph_add_fill(program->graph, 1, 1, color.data.color, &result.data.image.node);
  if (status == BB_STATUS_OK) status = bb_image_graph_attach_span(program->graph, result.data.image.node, call_info.span);
  if (status != BB_STATUS_OK) {
    bb_program_fail(program, status);
    result = bb_semantic_error();
  }

cleanup:
  bb_semantic_release(&color);
  return result;
}

bb_semantic_value bb_program_call_artifact(
  bb_program *program,
  const bb_syntax_tree *syntax,
  bb_syntax_node call,
  bb_syntax_node_info call_info
) {
  bb_semantic_value path = bb_semantic_error();
  bb_semantic_value image = bb_semantic_error();
  bb_semantic_value result = bb_semantic_error();
  bb_string_view logical_path;
  if (call_info.child_count != 3) {
    bb_program_diagnostic_span(
      program,
      BB_SEMANTIC_DIAGNOSTIC_ARGUMENT_COUNT,
      "artifact() expects a logical path and an image.",
      call_info.span
    );
    return result;
  }
  path = bb_program_evaluate(program, syntax, bb_program_child(syntax, call, 1));
  image = bb_program_evaluate(program, syntax, bb_program_child(syntax, call, 2));
  if (path.type != BB_SEMANTIC_STRING || !bb_program_unquote(path.data.string, &logical_path) ||
      image.type != BB_SEMANTIC_IMAGE) {
    bb_program_diagnostic_span(
      program,
      BB_SEMANTIC_DIAGNOSTIC_TYPE_MISMATCH,
      "artifact() expects a string logical path and an image.",
      call_info.span
    );
    goto cleanup;
  }
  result = image;
  result.data.image.key = logical_path;
  result.data.image.path = logical_path;

cleanup:
  bb_semantic_release(&image);
  bb_semantic_release(&path);
  return result;
}
