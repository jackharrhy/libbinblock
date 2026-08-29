import type { BinBlockGraphNode, BinBlockGradientStop, BinBlockImage, BinBlockRuntime } from './binblock.js';

export interface WebGlRenderResult extends BinBlockImage {
  equivalence: 'exact' | 'bounded';
  maxChannelError: number;
}

interface CompiledPipeline {
  program: WebGLProgram;
  width: number;
  height: number;
  equivalence: 'exact' | 'bounded';
}

function channel(rgba: number, shift: number): string {
  return (((rgba >>> shift) & 0xff) / 255).toFixed(9);
}

function color(rgba: number): string {
  return `vec4(${channel(rgba, 24)},${channel(rgba, 16)},${channel(rgba, 8)},${channel(rgba, 0)})`;
}

function easing(expression: string, kind: number): string {
  if (kind === 1) return `(${expression})*(${expression})*(3.0-2.0*(${expression}))`;
  if (kind === 2)
    return `0.5*(${expression})+0.5*(3.0*(${expression})*(${expression})-2.0*(${expression})*(${expression})*(${expression}))`;
  return expression;
}

function gradientSample(position: string, stops: readonly BinBlockGradientStop[], defaultEasing: number): string {
  if (stops.length < 2 || stops.length > 16) throw new Error('unsupported gradient stop count');
  const lines = [`float t=clamp(${position},0.0,1.0);`];
  for (let index = 0; index < stops.length; index += 1) {
    const right = stops[index];
    const left = stops[Math.max(0, index - 1)];
    const condition = index === stops.length - 1 ? '' : `if(t<=${right.offset.toFixed(17)})`;
    const denominator = right.offset - left.offset;
    const raw = denominator === 0 ? '0.0' : `clamp((t-${left.offset.toFixed(17)})/${denominator.toFixed(17)},0.0,1.0)`;
    const easingKind = right.hasEasing ? right.easing : left.hasEasing ? left.easing : defaultEasing;
    const result = `mix(${color(left.rgba)},${color(right.rgba)},${easing(raw, easingKind)})`;
    if (index === stops.length - 1) lines.push(`return ${result};`);
    else lines.push(`${condition}{return ${result};}`);
  }
  return lines.join('');
}

function nodeFunction(node: BinBlockGraphNode): { source: string; bounded: boolean } {
  const name = `bb_n${node.id}`;
  const child = (index: number, point = 'p'): string => `bb_n${node.inputs[index]}(${point})`;
  if (node.kind === 1) return { source: `vec4 ${name}(vec2 p){return ${color(node.options[0])};}`, bounded: false };
  if (node.kind === 2) {
    if (node.auxCount !== 0 || node.options[4] !== 0) throw new Error('unsupported exact alpha field');
    const [centerX, centerY, radius] = node.scalars;
    let distance: string;
    if (node.options[0] === 0) distance = `(p.x-${centerX})`;
    else if (node.options[0] === 1) distance = `(p.y-${centerY})`;
    else if (node.options[0] === 2) distance = `length(p-vec2(${centerX},${centerY}))`;
    else if (node.options[0] === 3) distance = `max(abs(p.x-${centerX}),abs(p.y-${centerY}))`;
    else distance = `min(min(p.x,p.y),min(${node.width - 1}.0-p.x,${node.height - 1}.0-p.y))`;
    const amount = easing(`clamp(${distance}/${radius},0.0,1.0)`, node.options[2]);
    const alpha = node.options[1] === 1 ? `(1.0-(${amount}))` : amount;
    const base = color(node.options[3]);
    return { source: `vec4 ${name}(vec2 p){vec4 c=${base};c.a*=(${alpha});return c;}`, bounded: true };
  }
  if (node.kind === 3) {
    const preset = node.options[0];
    const turns = (((node.options[1] | 0) % 4) + 4) % 4;
    const nx = node.width === 1 ? '0.5' : `p.x/${node.width - 1}.0`;
    const ny = node.height === 1 ? '0.5' : `p.y/${node.height - 1}.0`;
    const rotated =
      turns === 1
        ? `vec2(1.0-(${ny}),${nx})`
        : turns === 2
          ? `vec2(1.0-(${nx}),1.0-(${ny}))`
          : turns === 3
            ? `vec2(${ny},1.0-(${nx}))`
            : `vec2(${nx},${ny})`;
    const amounts = [
      '1.0-q.y',
      'q.y',
      '1.0-q.x',
      'q.x',
      'max(0.0,1.0-length(q-0.5)/sqrt(0.5))',
      'min(1.0,length(q-0.5)/sqrt(0.5))',
      '1.0-(q.x+q.y)/2.0',
      '1.0-max(q.x,q.y)',
    ];
    const rgb = color(node.options[2]);
    return {
      source: `vec4 ${name}(vec2 p){vec2 q=${rotated};vec4 c=${rgb};c.a=clamp(${amounts[preset]},0.0,1.0);return c;}`,
      bounded: true,
    };
  }
  if (node.kind === 4) {
    const angle = node.scalars[0] * (Math.PI / 180);
    let dx = Math.sin(angle);
    let dy = -Math.cos(angle);
    if (Math.abs(dx) < 1e-12) dx = 0;
    if (Math.abs(Math.abs(dx) - 1) < 1e-12) dx = Math.sign(dx);
    if (Math.abs(dy) < 1e-12) dy = 0;
    if (Math.abs(Math.abs(dy) - 1) < 1e-12) dy = Math.sign(dy);
    const extent = (Math.abs(dx) * Math.max(1, node.width - 1) + Math.abs(dy) * Math.max(1, node.height - 1)) / 2 || 1;
    const length = node.options[0] ? node.scalars[1] : 2 * extent;
    const position = `((p.x-${(node.width - 1) / 2})*${dx}+(p.y-${(node.height - 1) / 2})*${dy}+${extent})/${length}`;
    return { source: `vec4 ${name}(vec2 p){${gradientSample(position, node.stops, node.options[1])}}`, bounded: true };
  }
  if (node.kind === 5) {
    if (node.options[1] !== 0) throw new Error('unsupported legacy ellipse rounding');
    const [cx, cy, rx, ry, rotation] = node.scalars;
    const cosine = Math.cos(rotation);
    const sine = Math.sin(rotation);
    const position = `length(vec2((p.x-${cx})*${cosine}+(p.y-${cy})*${sine},(-(p.x-${cx})*${sine}+(p.y-${cy})*${cosine})) / vec2(${rx},${ry}))`;
    return { source: `vec4 ${name}(vec2 p){${gradientSample(position, node.stops, node.options[0])}}`, bounded: true };
  }
  if (node.kind === 9)
    return { source: `vec4 ${name}(vec2 p){vec4 c=${child(0)};c.a*=clamp(${node.scalars[0]},0.0,1.0);return c;}`, bounded: true };
  if (node.kind === 11) {
    const alpha = node.options[0] === 1 ? 'm.a' : 'c.a*m.a';
    return { source: `vec4 ${name}(vec2 p){vec4 c=${child(0)};vec4 m=${child(1)};c.a=${alpha};return c;}`, bounded: true };
  }
  if (node.kind === 12) {
    const sourceWidth = '__SOURCE_WIDTH__';
    const sourceHeight = '__SOURCE_HEIGHT__';
    const qx = node.width === 1 ? '0.0' : `p.x*(${sourceWidth}-1.0)/${node.width - 1}.0`;
    const qy = node.height === 1 ? '0.0' : `p.y*(${sourceHeight}-1.0)/${node.height - 1}.0`;
    return { source: `vec4 ${name}(vec2 p){return ${child(0, `vec2(${qx},${qy})`)};}`, bounded: true };
  }
  throw new Error(`unsupported WebGL node kind ${node.kind}`);
}

function shader(
  runtime: BinBlockRuntime,
  root: number,
): { fragment: string; width: number; height: number; equivalence: 'exact' | 'bounded' } {
  const nodes: BinBlockGraphNode[] = [];
  const visit = (id: number): void => {
    if (nodes.some((node) => node.id === id)) return;
    const node = runtime.graphNode(id);
    for (const input of node.inputs) visit(input);
    nodes.push(node);
  };
  visit(root);
  let bounded = false;
  const functions = nodes.map((node) => {
    const lowered = nodeFunction(node);
    bounded ||= lowered.bounded;
    if (node.kind !== 12) return lowered.source;
    const source = nodes.find((candidate) => candidate.id === node.inputs[0]);
    if (!source) throw new Error('missing resize source');
    return lowered.source.replace('__SOURCE_WIDTH__', `${source.width}.0`).replace('__SOURCE_HEIGHT__', `${source.height}.0`);
  });
  const output = nodes[nodes.length - 1];
  return {
    width: output.width,
    height: output.height,
    equivalence: bounded ? 'bounded' : 'exact',
    fragment: `#version 300 es
precision highp float;
uniform vec2 u_size;
out vec4 out_color;
${functions.join('\n')}
void main(){vec2 p=gl_FragCoord.xy-vec2(0.5);out_color=bb_n${root}(p);}`,
  };
}

function compileShader(gl: WebGL2RenderingContext, type: number, source: string): WebGLShader {
  const value = gl.createShader(type);
  if (!value) throw new Error('WebGL shader allocation failed.');
  gl.shaderSource(value, source);
  gl.compileShader(value);
  if (!gl.getShaderParameter(value, gl.COMPILE_STATUS)) {
    const message = gl.getShaderInfoLog(value) ?? 'unknown shader error';
    gl.deleteShader(value);
    throw new Error(message);
  }
  return value;
}

export class BinBlockWebGl2Backend {
  readonly #canvas: HTMLCanvasElement;
  readonly #gl: WebGL2RenderingContext | null;
  readonly #pipelines = new Map<string, CompiledPipeline>();
  #lost = false;

  constructor(canvas: HTMLCanvasElement = document.createElement('canvas')) {
    this.#canvas = canvas;
    this.#gl = canvas.getContext('webgl2', { alpha: true, antialias: false, depth: false, premultipliedAlpha: false });
    canvas.addEventListener('webglcontextlost', (event) => {
      event.preventDefault();
      this.#lost = true;
      this.#pipelines.clear();
    });
    canvas.addEventListener('webglcontextrestored', () => {
      this.#lost = false;
    });
  }

  get available(): boolean {
    return this.#gl !== null && !this.#lost;
  }

  render(runtime: BinBlockRuntime, root: number): WebGlRenderResult | undefined {
    const gl = this.#gl;
    if (!gl || this.#lost) return undefined;
    const key = `${runtime.generation}:${root}`;
    let pipeline = this.#pipelines.get(key);
    if (!pipeline) {
      let lowered: ReturnType<typeof shader>;
      try {
        lowered = shader(runtime, root);
      } catch {
        return undefined;
      }
      const vertex = compileShader(
        gl,
        gl.VERTEX_SHADER,
        `#version 300 es
in vec2 a_position;
void main(){gl_Position=vec4(a_position,0.0,1.0);}`,
      );
      const fragment = compileShader(gl, gl.FRAGMENT_SHADER, lowered.fragment);
      const program = gl.createProgram();
      if (!program) return undefined;
      gl.attachShader(program, vertex);
      gl.attachShader(program, fragment);
      gl.linkProgram(program);
      gl.deleteShader(vertex);
      gl.deleteShader(fragment);
      if (!gl.getProgramParameter(program, gl.LINK_STATUS)) {
        gl.deleteProgram(program);
        return undefined;
      }
      pipeline = { program, width: lowered.width, height: lowered.height, equivalence: lowered.equivalence };
      this.#pipelines.set(key, pipeline);
    }
    this.#canvas.width = pipeline.width;
    this.#canvas.height = pipeline.height;
    gl.viewport(0, 0, pipeline.width, pipeline.height);
    gl.useProgram(pipeline.program);
    const buffer = gl.createBuffer();
    gl.bindBuffer(gl.ARRAY_BUFFER, buffer);
    gl.bufferData(gl.ARRAY_BUFFER, new Float32Array([-1, -1, 3, -1, -1, 3]), gl.STREAM_DRAW);
    const position = gl.getAttribLocation(pipeline.program, 'a_position');
    gl.enableVertexAttribArray(position);
    gl.vertexAttribPointer(position, 2, gl.FLOAT, false, 0, 0);
    gl.uniform2f(gl.getUniformLocation(pipeline.program, 'u_size'), pipeline.width, pipeline.height);
    gl.drawArrays(gl.TRIANGLES, 0, 3);
    const pixels = new Uint8ClampedArray(pipeline.width * pipeline.height * 4);
    gl.readPixels(0, 0, pipeline.width, pipeline.height, gl.RGBA, gl.UNSIGNED_BYTE, pixels);
    gl.deleteBuffer(buffer);
    return {
      width: pipeline.width,
      height: pipeline.height,
      pixels,
      equivalence: pipeline.equivalence,
      maxChannelError: pipeline.equivalence === 'exact' ? 0 : 1,
    };
  }

  loseContext(): boolean {
    const extension = this.#gl?.getExtension('WEBGL_lose_context');
    if (!extension) return false;
    this.#lost = true;
    this.#pipelines.clear();
    extension.loseContext();
    return true;
  }

  dispose(): void {
    if (this.#gl) for (const pipeline of this.#pipelines.values()) this.#gl.deleteProgram(pipeline.program);
    this.#pipelines.clear();
  }
}
