struct Brush {
  kind_data: vec4<u32>,
  color: vec4<f32>,
  parameters0: vec4<f32>,
  parameters1: vec4<f32>,
  size_opacity: vec4<f32>,
}

struct Item {
  destination_rect: vec4<f32>,
  source_size: vec4<f32>,
  composite: vec4<f32>,
  base: Brush,
  overlay: Brush,
}

struct Stop {
  color: vec4<f32>,
  parameters: vec4<f32>,
}

@group(2) @binding(0) var<storage, read> items: array<Item>;
@group(2) @binding(1) var<storage, read> stops: array<Stop>;

fn apply_easing(value: f32, kind: u32) -> f32 {
  let t = clamp(value, 0.0, 1.0);
  if (kind == 1u) { return t * t * (3.0 - 2.0 * t); }
  if (kind == 2u) { return 0.5 * t + 0.5 * (3.0 * t * t - 2.0 * t * t * t); }
  return t;
}

fn quantize(value: vec4<f32>) -> vec4<f32> {
  return floor(clamp(value, vec4<f32>(0.0), vec4<f32>(1.0)) * 255.0 + vec4<f32>(0.5)) / 255.0;
}

fn sample_stops(offset: u32, count: u32, default_easing: u32, position: f32) -> vec4<f32> {
  let t = clamp(position, 0.0, 1.0);
  var right_index = 0u;
  loop {
    if (right_index >= count || stops[offset + right_index].parameters.x >= t) { break; }
    right_index += 1u;
  }
  if (right_index == count) { right_index = count - 1u; }
  var left_index = 0u;
  if (right_index != 0u) { left_index = right_index - 1u; }
  let left = stops[offset + left_index];
  let right = stops[offset + right_index];
  let denominator = right.parameters.x - left.parameters.x;
  var amount = 0.0;
  if (denominator != 0.0) { amount = (t - left.parameters.x) / denominator; }
  var easing = default_easing;
  if (left.parameters.z != 0.0) { easing = u32(left.parameters.y); }
  if (right.parameters.z != 0.0) { easing = u32(right.parameters.y); }
  return mix(left.color, right.color, apply_easing(amount, easing));
}

fn evaluate_brush(brush: Brush, pixel: vec2<f32>) -> vec4<f32> {
  if (pixel.x < 0.0 || pixel.y < 0.0 ||
      pixel.x >= brush.size_opacity.x || pixel.y >= brush.size_opacity.y) {
    return vec4<f32>(0.0);
  }
  var result = vec4<f32>(0.0);
  if (brush.kind_data.x == 1u) {
    result = brush.color;
  } else if (brush.kind_data.x == 2u) {
    var nx = 0.5;
    var ny = 0.5;
    if (brush.size_opacity.x != 1.0) { nx = pixel.x / (brush.size_opacity.x - 1.0); }
    if (brush.size_opacity.y != 1.0) { ny = pixel.y / (brush.size_opacity.y - 1.0); }
    var q = vec2<f32>(nx, ny);
    if (brush.kind_data.z == 1u) { q = vec2<f32>(1.0 - ny, nx); }
    else if (brush.kind_data.z == 2u) { q = vec2<f32>(1.0 - nx, 1.0 - ny); }
    else if (brush.kind_data.z == 3u) { q = vec2<f32>(ny, 1.0 - nx); }
    var amount: f32;
    if (brush.kind_data.y == 0u) { amount = 1.0 - q.y; }
    else if (brush.kind_data.y == 1u) { amount = q.y; }
    else if (brush.kind_data.y == 2u) { amount = 1.0 - q.x; }
    else if (brush.kind_data.y == 3u) { amount = q.x; }
    else if (brush.kind_data.y == 4u) { amount = max(0.0, 1.0 - length(q - vec2<f32>(0.5)) / sqrt(0.5)); }
    else if (brush.kind_data.y == 5u) { amount = min(1.0, length(q - vec2<f32>(0.5)) / sqrt(0.5)); }
    else if (brush.kind_data.y == 6u) { amount = 1.0 - (q.x + q.y) / 2.0; }
    else { amount = 1.0 - max(q.x, q.y); }
    result = vec4<f32>(brush.color.rgb, clamp(amount, 0.0, 1.0));
  } else if (brush.kind_data.x == 3u) {
    let center = (brush.size_opacity.xy - vec2<f32>(1.0)) * 0.5;
    let position = (dot(pixel - center, brush.parameters0.xy) + brush.parameters0.z) / brush.parameters0.w;
    result = sample_stops(brush.kind_data.y, brush.kind_data.z, brush.kind_data.w, position);
  } else if (brush.kind_data.x == 4u) {
    let delta = pixel - brush.parameters0.xy;
    let rotated = vec2<f32>(
      delta.x * brush.parameters1.x + delta.y * brush.parameters1.y,
      -delta.x * brush.parameters1.y + delta.y * brush.parameters1.x,
    );
    let position = length(rotated / brush.parameters0.zw);
    result = sample_stops(brush.kind_data.y, brush.kind_data.z, brush.kind_data.w, position);
  } else if (brush.kind_data.x == 5u) {
    let delta = pixel - brush.parameters0.xy;
    var distance_value: f32;
    if (brush.kind_data.y == 0u) { distance_value = delta.x; }
    else if (brush.kind_data.y == 1u) { distance_value = delta.y; }
    else if (brush.kind_data.y == 2u) { distance_value = length(delta); }
    else if (brush.kind_data.y == 3u) { distance_value = max(abs(delta.x), abs(delta.y)); }
    else {
      distance_value = min(min(pixel.x, pixel.y), min(
        brush.size_opacity.x - 1.0 - pixel.x,
        brush.size_opacity.y - 1.0 - pixel.y,
      ));
    }
    let amount = apply_easing(distance_value / brush.parameters0.z, brush.kind_data.w);
    var alpha = amount;
    if (brush.kind_data.z == 1u) { alpha = 1.0 - amount; }
    alpha = floor(clamp(alpha, 0.0, 1.0) * 255.0 + 0.5) / 255.0;
    result = vec4<f32>(brush.color.rgb, alpha * brush.color.a);
  }
  result.a *= brush.size_opacity.z;
  return quantize(result);
}

fn source_over(destination: vec4<f32>, source: vec4<f32>, opacity: f32) -> vec4<f32> {
  let source_alpha = source.a * opacity;
  let output_alpha = source_alpha + destination.a * (1.0 - source_alpha);
  if (output_alpha == 0.0) { return vec4<f32>(0.0); }
  let rgb = (
    source.rgb * source_alpha + destination.rgb * destination.a * (1.0 - source_alpha)
  ) / output_alpha;
  return quantize(vec4<f32>(rgb, output_alpha));
}

@fragment
fn main(
  @location(0) fragment_pixel: vec2<f32>,
  @location(1) @interpolate(flat) fragment_item: u32,
) -> @location(0) vec4<f32> {
  let item = items[fragment_item];
  var color = evaluate_brush(item.base, fragment_pixel);
  if (item.overlay.kind_data.x != 0u) {
    let overlay_pixel = fragment_pixel - item.composite.xy;
    color = source_over(color, evaluate_brush(item.overlay, overlay_pixel), item.composite.z);
  }
  color.a *= item.composite.w;
  return quantize(color);
}
