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

struct TargetUniforms {
  target_size: vec4<f32>,
}

@group(0) @binding(0) var<storage, read> items: array<Item>;
@group(1) @binding(0) var<uniform> target_uniforms: TargetUniforms;

struct VertexOutput {
  @builtin(position) position: vec4<f32>,
  @location(0) fragment_pixel: vec2<f32>,
  @location(1) @interpolate(flat) fragment_item: u32,
}

@vertex
fn main(@builtin(vertex_index) vertex_index: u32, @builtin(instance_index) instance_index: u32) -> VertexOutput {
  let corners = array<vec2<f32>, 6>(
    vec2<f32>(0.0, 0.0), vec2<f32>(1.0, 0.0), vec2<f32>(0.0, 1.0),
    vec2<f32>(0.0, 1.0), vec2<f32>(1.0, 0.0), vec2<f32>(1.0, 1.0),
  );
  let item = items[instance_index];
  let corner = corners[vertex_index];
  let target_pixel = item.destination_rect.xy + corner * item.destination_rect.zw;
  let clip = vec2<f32>(
    target_pixel.x * 2.0 / target_uniforms.target_size.x - 1.0,
    1.0 - target_pixel.y * 2.0 / target_uniforms.target_size.y,
  );
  var output: VertexOutput;
  output.position = vec4<f32>(clip, 0.0, 1.0);
  output.fragment_pixel = corner * item.source_size.xy - vec2<f32>(0.5);
  output.fragment_item = instance_index;
  return output;
}
