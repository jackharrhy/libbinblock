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

layout(std430, set = 0, binding = 0) readonly buffer ItemBuffer {
  Item items[];
};

layout(std140, set = 1, binding = 0) uniform TargetUniforms {
  vec4 target_size;
};

layout(location = 0) out vec2 fragment_pixel;
layout(location = 1) flat out uint fragment_item;

const vec2 corners[6] = vec2[6](
  vec2(0.0, 0.0), vec2(1.0, 0.0), vec2(0.0, 1.0),
  vec2(0.0, 1.0), vec2(1.0, 0.0), vec2(1.0, 1.0)
);

void main() {
  Item item = items[gl_InstanceIndex];
  vec2 corner = corners[gl_VertexIndex];
  vec2 target_pixel = item.target.xy + corner * item.target.zw;
  vec2 clip = vec2(
    target_pixel.x * 2.0 / target_size.x - 1.0,
    1.0 - target_pixel.y * 2.0 / target_size.y
  );
  gl_Position = vec4(clip, 0.0, 1.0);
  fragment_pixel = corner * item.source.xy - vec2(0.5);
  fragment_item = gl_InstanceIndex;
}
