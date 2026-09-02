#version 450

struct Brush {
  uvec4 meta;
  vec4 color;
  vec4 parameters0;
  vec4 parameters1;
  vec4 size_opacity;
};

struct Item {
  vec4 target;
  vec4 source;
  vec4 composite;
  Brush base;
  Brush overlay;
};

struct Stop {
  vec4 color;
  vec4 parameters;
};

layout(std430, set = 2, binding = 0) readonly buffer ItemBuffer {
  Item items[];
};

layout(std430, set = 2, binding = 1) readonly buffer StopBuffer {
  Stop stops[];
};

layout(location = 0) in vec2 fragment_pixel;
layout(location = 1) flat in uint fragment_item;
layout(location = 0) out vec4 output_color;

float apply_easing(float value, uint kind) {
  float t = clamp(value, 0.0, 1.0);
  if (kind == 1u) return t * t * (3.0 - 2.0 * t);
  if (kind == 2u) return 0.5 * t + 0.5 * (3.0 * t * t - 2.0 * t * t * t);
  return t;
}

vec4 quantize(vec4 value) {
  return floor(clamp(value, 0.0, 1.0) * 255.0 + 0.5) / 255.0;
}

vec4 sample_stops(uint offset, uint count, uint default_easing, float position) {
  float t = clamp(position, 0.0, 1.0);
  uint right_index = 0u;
  while (right_index < count && stops[offset + right_index].parameters.x < t) right_index++;
  if (right_index == count) right_index = count - 1u;
  uint left_index = right_index == 0u ? 0u : right_index - 1u;
  Stop left = stops[offset + left_index];
  Stop right = stops[offset + right_index];
  float denominator = right.parameters.x - left.parameters.x;
  float amount = denominator == 0.0 ? 0.0 : (t - left.parameters.x) / denominator;
  uint easing = right.parameters.z != 0.0
    ? uint(right.parameters.y)
    : left.parameters.z != 0.0 ? uint(left.parameters.y) : default_easing;
  return mix(left.color, right.color, apply_easing(amount, easing));
}

vec4 evaluate_brush(Brush brush, vec2 pixel) {
  if (pixel.x < 0.0 || pixel.y < 0.0 ||
      pixel.x >= brush.size_opacity.x || pixel.y >= brush.size_opacity.y) return vec4(0.0);
  vec4 result = vec4(0.0);
  if (brush.meta.x == 1u) {
    result = brush.color;
  } else if (brush.meta.x == 2u) {
    float nx = brush.size_opacity.x == 1.0 ? 0.5 : pixel.x / (brush.size_opacity.x - 1.0);
    float ny = brush.size_opacity.y == 1.0 ? 0.5 : pixel.y / (brush.size_opacity.y - 1.0);
    vec2 q = vec2(nx, ny);
    if (brush.meta.z == 1u) q = vec2(1.0 - ny, nx);
    else if (brush.meta.z == 2u) q = vec2(1.0 - nx, 1.0 - ny);
    else if (brush.meta.z == 3u) q = vec2(ny, 1.0 - nx);
    float amount;
    if (brush.meta.y == 0u) amount = 1.0 - q.y;
    else if (brush.meta.y == 1u) amount = q.y;
    else if (brush.meta.y == 2u) amount = 1.0 - q.x;
    else if (brush.meta.y == 3u) amount = q.x;
    else if (brush.meta.y == 4u) amount = max(0.0, 1.0 - length(q - vec2(0.5)) / sqrt(0.5));
    else if (brush.meta.y == 5u) amount = min(1.0, length(q - vec2(0.5)) / sqrt(0.5));
    else if (brush.meta.y == 6u) amount = 1.0 - (q.x + q.y) / 2.0;
    else amount = 1.0 - max(q.x, q.y);
    result = vec4(brush.color.rgb, clamp(amount, 0.0, 1.0));
  } else if (brush.meta.x == 3u) {
    vec2 center = (brush.size_opacity.xy - vec2(1.0)) * 0.5;
    float position = (dot(pixel - center, brush.parameters0.xy) + brush.parameters0.z) / brush.parameters0.w;
    result = sample_stops(brush.meta.y, brush.meta.z, brush.meta.w, position);
  } else if (brush.meta.x == 4u) {
    vec2 delta = pixel - brush.parameters0.xy;
    vec2 rotated = vec2(
      delta.x * brush.parameters1.x + delta.y * brush.parameters1.y,
      -delta.x * brush.parameters1.y + delta.y * brush.parameters1.x
    );
    float position = length(rotated / brush.parameters0.zw);
    result = sample_stops(brush.meta.y, brush.meta.z, brush.meta.w, position);
  } else if (brush.meta.x == 5u) {
    vec2 delta = pixel - brush.parameters0.xy;
    float distance_value;
    if (brush.meta.y == 0u) distance_value = delta.x;
    else if (brush.meta.y == 1u) distance_value = delta.y;
    else if (brush.meta.y == 2u) distance_value = length(delta);
    else if (brush.meta.y == 3u) distance_value = max(abs(delta.x), abs(delta.y));
    else distance_value = min(min(pixel.x, pixel.y), min(
      brush.size_opacity.x - 1.0 - pixel.x,
      brush.size_opacity.y - 1.0 - pixel.y
    ));
    float amount = apply_easing(distance_value / brush.parameters0.z, brush.meta.w);
    float alpha = brush.meta.z == 1u ? 1.0 - amount : amount;
    alpha = floor(clamp(alpha, 0.0, 1.0) * 255.0 + 0.5) / 255.0;
    result = vec4(brush.color.rgb, alpha * brush.color.a);
  }
  result.a *= brush.size_opacity.z;
  return quantize(result);
}

vec4 source_over(vec4 destination, vec4 source, float opacity) {
  float source_alpha = source.a * opacity;
  float output_alpha = source_alpha + destination.a * (1.0 - source_alpha);
  if (output_alpha == 0.0) return vec4(0.0);
  vec3 rgb = (
    source.rgb * source_alpha + destination.rgb * destination.a * (1.0 - source_alpha)
  ) / output_alpha;
  return quantize(vec4(rgb, output_alpha));
}

void main() {
  Item item = items[fragment_item];
  vec4 color = evaluate_brush(item.base, fragment_pixel);
  if (item.overlay.meta.x != 0u) {
    vec2 overlay_pixel = fragment_pixel - item.composite.xy;
    color = source_over(color, evaluate_brush(item.overlay, overlay_pixel), item.composite.z);
  }
  color.a *= item.composite.w;
  output_color = quantize(color);
}
