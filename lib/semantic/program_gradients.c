#include "program_internal.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

bb_semantic_value bb_program_call_palette(
  bb_program *program,
  const bb_syntax_tree *syntax,
  bb_syntax_node call,
  bb_syntax_node_info call_info
) {
  bb_value *rows = NULL;
  bb_collection *collection = NULL;
  bb_semantic_value result = bb_semantic_error();
  const size_t entry_count = call_info.child_count > 0 ? call_info.child_count - 1 : 0;
  size_t value_count;
  size_t bytes;
  size_t index;
  bb_status status;
  if (entry_count == 0 || !bb_size_multiply(entry_count, 2, &value_count) ||
      !bb_size_multiply(value_count, sizeof(*rows), &bytes)) {
    bb_program_diagnostic_span(
      program,
      BB_SEMANTIC_DIAGNOSTIC_ARGUMENT_COUNT,
      "palette() expects one or more named color arguments.",
      call_info.span
    );
    return result;
  }
  status = bb_context_allocate(program->context, bytes, _Alignof(bb_value), (void **)&rows);
  if (status != BB_STATUS_OK) {
    bb_program_fail(program, status);
    return result;
  }
  memset(rows, 0, bytes);
  for (index = 0; index < entry_count; index += 1) {
    const bb_syntax_node argument = bb_program_child(syntax, call, index + 1);
    bb_syntax_node_info argument_info;
    bb_semantic_value color;
    if (bb_program_node_info(syntax, argument, &argument_info) != BB_STATUS_OK ||
        argument_info.kind != BB_SYNTAX_NAMED_ARGUMENT) {
      bb_program_diagnostic_span(
        program,
        BB_SEMANTIC_DIAGNOSTIC_TYPE_MISMATCH,
        "palette() entries must be named colors.",
        argument_info.span
      );
      goto cleanup;
    }
    color = bb_program_evaluate(program, syntax, bb_program_child(syntax, argument, 1));
    if (color.type != BB_SEMANTIC_COLOR) {
      bb_program_diagnostic_span(
        program,
        BB_SEMANTIC_DIAGNOSTIC_TYPE_MISMATCH,
        "palette() entry values must be colors.",
        argument_info.span
      );
      bb_semantic_release(&color);
      goto cleanup;
    }
    rows[index * 2].kind = BB_VALUE_STRING;
    rows[index * 2].data.string = bb_program_node_text(syntax, bb_program_child(syntax, argument, 0));
    rows[index * 2 + 1].kind = BB_VALUE_COLOR;
    rows[index * 2 + 1].data.color = color.data.color;
    bb_semantic_release(&color);
  }
  status = bb_collection_from_rows(program->context, rows, entry_count, 2, &collection);
  if (status != BB_STATUS_OK) {
    bb_program_fail(program, status);
    goto cleanup;
  }
  result.type = BB_SEMANTIC_COLLECTION;
  result.data.collection.plan = collection;
  result.data.collection.shape = BB_SEMANTIC_COLLECTION_PALETTE;

cleanup:
  bb_context_deallocate(program->context, rows, bytes, _Alignof(bb_value));
  return result;
}

bb_semantic_value bb_program_call_linear_gradient(
  bb_program *program,
  const bb_syntax_tree *syntax,
  bb_syntax_node call,
  bb_syntax_node_info call_info
) {
  const size_t argument_count = call_info.child_count > 0 ? call_info.child_count - 1 : 0;
  bb_gradient_stop *stops = NULL;
  size_t stop_count;
  size_t stop_bytes;
  bb_semantic_value angle = bb_semantic_error();
  bb_semantic_value result = bb_semantic_error();
  bb_linear_gradient_desc desc;
  size_t index;
  bb_status status;
  if (argument_count < 3) {
    bb_program_diagnostic_span(
      program,
      BB_SEMANTIC_DIAGNOSTIC_ARGUMENT_COUNT,
      "lg() expects an angle and at least two color stops.",
      call_info.span
    );
    return result;
  }
  angle = bb_program_evaluate(program, syntax, bb_program_child(syntax, call, 1));
  if (angle.type != BB_SEMANTIC_DEGREES) {
    bb_program_diagnostic_span(
      program,
      BB_SEMANTIC_DIAGNOSTIC_TYPE_MISMATCH,
      "Linear gradient angles must use degrees.",
      call_info.span
    );
    bb_semantic_release(&angle);
    return result;
  }
  stop_count = argument_count - 1;
  if (!bb_size_multiply(stop_count, sizeof(*stops), &stop_bytes)) {
    bb_program_fail(program, BB_STATUS_OVERFLOW);
    return result;
  }
  status = bb_context_allocate(program->context, stop_bytes, _Alignof(bb_gradient_stop), (void **)&stops);
  if (status != BB_STATUS_OK) {
    bb_program_fail(program, status);
    return result;
  }
  memset(stops, 0, stop_bytes);
  for (index = 0; index < stop_count; index += 1) {
    const bb_syntax_node argument = bb_program_child(syntax, call, index + 2);
    bb_syntax_node_info argument_info;
    bb_syntax_node color_node = argument;
    bb_semantic_value color;
    stops[index].offset = NAN;
    (void)bb_program_node_info(syntax, argument, &argument_info);
    if (argument_info.kind == BB_SYNTAX_GRADIENT_STOP) {
      bb_semantic_value offset;
      color_node = bb_program_child(syntax, argument, 0);
      offset = bb_program_evaluate(program, syntax, bb_program_child(syntax, argument, 1));
      if (offset.type != BB_SEMANTIC_PERCENTAGE || offset.data.number < 0.0 || offset.data.number > 100.0) {
        bb_program_diagnostic_span(
          program,
          BB_SEMANTIC_DIAGNOSTIC_TYPE_MISMATCH,
          "Gradient stop offsets must be between 0% and 100%.",
          argument_info.span
        );
        bb_semantic_release(&offset);
        goto cleanup;
      }
      stops[index].offset = offset.data.number / 100.0;
      bb_semantic_release(&offset);
    }
    color = bb_program_evaluate(program, syntax, color_node);
    if (color.type != BB_SEMANTIC_COLOR) {
      bb_program_diagnostic_span(
        program,
        BB_SEMANTIC_DIAGNOSTIC_TYPE_MISMATCH,
        "Gradient stops must be colors.",
        argument_info.span
      );
      bb_semantic_release(&color);
      goto cleanup;
    }
    stops[index].color = color.data.color;
    stops[index].easing = BB_EASING_LINEAR;
    bb_semantic_release(&color);
  }
  if (isnan(stops[0].offset)) stops[0].offset = 0.0;
  if (isnan(stops[stop_count - 1].offset)) stops[stop_count - 1].offset = 1.0;
  index = 1;
  while (index + 1 < stop_count) {
    size_t end;
    size_t fill;
    if (!isnan(stops[index].offset)) {
      index += 1;
      continue;
    }
    end = index + 1;
    while (end < stop_count && isnan(stops[end].offset)) end += 1;
    for (fill = index; fill < end; fill += 1) {
      const double fraction = (double)(fill - index + 1) / (double)(end - index + 1);
      stops[fill].offset = stops[index - 1].offset + (stops[end].offset - stops[index - 1].offset) * fraction;
    }
    index = end + 1;
  }
  memset(&desc, 0, sizeof(desc));
  desc.width = 64;
  desc.height = 64;
  desc.angle_degrees = angle.data.number;
  desc.stops = stops;
  desc.stop_count = stop_count;
  desc.easing = BB_EASING_LINEAR;
  result.type = BB_SEMANTIC_IMAGE;
  result.data.image.width = 64;
  result.data.image.height = 64;
  status = bb_image_graph_add_linear_gradient(program->graph, &desc, &result.data.image.node);
  if (status == BB_STATUS_OK) status = bb_image_graph_attach_span(program->graph, result.data.image.node, call_info.span);
  if (status != BB_STATUS_OK) {
    bb_program_fail(program, status);
    result = bb_semantic_error();
  }

cleanup:
  bb_context_deallocate(program->context, stops, stop_bytes, _Alignof(bb_gradient_stop));
  bb_semantic_release(&angle);
  return result;
}

bb_semantic_value bb_program_call_radial_gradient(
  bb_program *program,
  const bb_syntax_tree *syntax,
  bb_syntax_node call,
  bb_syntax_node_info call_info
) {
  const size_t argument_count = call_info.child_count > 0 ? call_info.child_count - 1 : 0;
  bb_semantic_value inner = bb_semantic_error();
  bb_semantic_value outer = bb_semantic_error();
  bb_semantic_value result = bb_semantic_error();
  bb_gradient_stop stops[2];
  bb_elliptical_gradient_desc desc;
  size_t index;
  size_t stop_index;
  bb_status status;
  if (argument_count < 2) {
    bb_program_diagnostic_span(
      program,
      BB_SEMANTIC_DIAGNOSTIC_ARGUMENT_COUNT,
      "rg() expects two colors followed by optional named geometry arguments.",
      call_info.span
    );
    return result;
  }
  memset(&desc, 0, sizeof(desc));
  memset(stops, 0, sizeof(stops));
  stops[0].offset = 0.0;
  stops[1].offset = 1.0;
  for (stop_index = 0; stop_index < 2; stop_index += 1) {
    const bb_syntax_node argument = bb_program_child(syntax, call, stop_index + 1);
    bb_syntax_node color_node = argument;
    bb_syntax_node_info argument_info;
    bb_semantic_value *color = stop_index == 0 ? &inner : &outer;
    (void)bb_program_node_info(syntax, argument, &argument_info);
    if (argument_info.kind == BB_SYNTAX_GRADIENT_STOP) {
      bb_semantic_value offset;
      color_node = bb_program_child(syntax, argument, 0);
      offset = bb_program_evaluate(program, syntax, bb_program_child(syntax, argument, 1));
      if (offset.type != BB_SEMANTIC_PERCENTAGE || offset.data.number < 0.0 || offset.data.number > 100.0) {
        bb_program_diagnostic_span(
          program,
          BB_SEMANTIC_DIAGNOSTIC_TYPE_MISMATCH,
          "Radial gradient stop offsets must be between 0% and 100%.",
          argument_info.span
        );
        bb_semantic_release(&offset);
        goto cleanup;
      }
      stops[stop_index].offset = offset.data.number / 100.0;
      bb_semantic_release(&offset);
    }
    *color = bb_program_evaluate(program, syntax, color_node);
    if (color->type != BB_SEMANTIC_COLOR) {
      bb_program_diagnostic_span(
        program,
        BB_SEMANTIC_DIAGNOSTIC_TYPE_MISMATCH,
        "rg() inner and outer stops must be colors.",
        argument_info.span
      );
      goto cleanup;
    }
    stops[stop_index].color = color->data.color;
  }
  desc.width = 64;
  desc.height = 64;
  desc.center_x = 31.5;
  desc.center_y = 31.5;
  desc.radius_x = 32.0;
  desc.radius_y = 32.0;
  desc.easing = BB_EASING_LINEAR;
  desc.stops = stops;
  desc.stop_count = 2;
  for (index = 2; index < argument_count; index += 1) {
    const bb_syntax_node argument = bb_program_child(syntax, call, index + 1);
    bb_syntax_node_info argument_info;
    bb_string_view name;
    bb_semantic_value value;
    double number;
    if (bb_program_node_info(syntax, argument, &argument_info) != BB_STATUS_OK ||
        argument_info.kind != BB_SYNTAX_NAMED_ARGUMENT) {
      bb_program_diagnostic_span(
        program,
        BB_SEMANTIC_DIAGNOSTIC_TYPE_MISMATCH,
        "rg() geometry arguments must be named.",
        argument_info.span
      );
      goto cleanup;
    }
    name = bb_program_node_text(syntax, bb_program_child(syntax, argument, 0));
    value = bb_program_evaluate(program, syntax, bb_program_child(syntax, argument, 1));
    if (bb_string_view_is(name, "center") && value.type == BB_SEMANTIC_VECTOR2) {
      desc.center_x = value.data.vector2.x;
      desc.center_y = value.data.vector2.y;
    } else if (bb_string_view_is(name, "radius") && bb_semantic_as_number(value, &number) && number > 0.0) {
      desc.radius_x = number;
      desc.radius_y = number;
    } else if (bb_string_view_is(name, "radius-x") && bb_semantic_as_number(value, &number) && number > 0.0)
      desc.radius_x = number;
    else if (bb_string_view_is(name, "radius-y") && bb_semantic_as_number(value, &number) && number > 0.0)
      desc.radius_y = number;
    else if (bb_string_view_is(name, "rotation") && value.type == BB_SEMANTIC_DEGREES)
      desc.rotation_radians = value.data.number * 0.017453292519943295769;
    else if (bb_string_view_is(name, "width") && value.type == BB_SEMANTIC_INTEGER &&
             value.data.integer > 0 && value.data.integer <= UINT32_MAX)
      desc.width = (uint32_t)value.data.integer;
    else if (bb_string_view_is(name, "height") && value.type == BB_SEMANTIC_INTEGER &&
             value.data.integer > 0 && value.data.integer <= UINT32_MAX)
      desc.height = (uint32_t)value.data.integer;
    else if (bb_string_view_is(name, "reference-rounding") && value.type == BB_SEMANTIC_BOOL)
      desc.reference_radial_rounding = !!value.data.boolean;
    else if (bb_string_view_is(name, "easing") && value.type == BB_SEMANTIC_STRING) {
      bb_string_view easing;
      if (!bb_program_unquote(value.data.string, &easing)) {
        bb_semantic_release(&value);
        goto invalid_argument;
      }
      if (bb_string_view_is(easing, "linear")) desc.easing = BB_EASING_LINEAR;
      else if (bb_string_view_is(easing, "smoothstep")) desc.easing = BB_EASING_SMOOTHSTEP;
      else if (bb_string_view_is(easing, "reference")) desc.easing = BB_EASING_REFERENCE;
      else {
        bb_semantic_release(&value);
        goto invalid_argument;
      }
    } else {
      bb_semantic_release(&value);
      goto invalid_argument;
    }
    bb_semantic_release(&value);
    continue;

invalid_argument:
    bb_program_diagnostic_span(
      program,
      BB_SEMANTIC_DIAGNOSTIC_TYPE_MISMATCH,
      "Invalid rg() named geometry argument.",
      argument_info.span
    );
    goto cleanup;
  }
  result.type = BB_SEMANTIC_IMAGE;
  result.data.image.width = desc.width;
  result.data.image.height = desc.height;
  status = bb_image_graph_add_elliptical_gradient(program->graph, &desc, &result.data.image.node);
  if (status == BB_STATUS_OK)
    status = bb_image_graph_attach_span(program->graph, result.data.image.node, call_info.span);
  if (status != BB_STATUS_OK) {
    bb_program_fail(program, status);
    result = bb_semantic_error();
  }

cleanup:
  bb_semantic_release(&outer);
  bb_semantic_release(&inner);
  return result;
}
