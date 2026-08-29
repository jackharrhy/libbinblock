export interface BinBlockWasmModule {
  HEAPU8: Uint8Array;
  HEAPU32: Uint32Array;
  _malloc(size: number): number;
  _free(pointer: number): void;
  _bb_wasm_session_create(): number;
  _bb_wasm_session_destroy(session: number): void;
  _bb_wasm_session_generation(session: number): number;
  _bb_wasm_session_clear_resources(session: number): void;
  _bb_wasm_session_add_module(
    session: number,
    specifier: number,
    specifierLength: number,
    identity: number,
    identityLength: number,
    source: number,
    sourceLength: number,
  ): number;
  _bb_wasm_session_add_asset_rgba(
    session: number,
    logicalId: number,
    logicalIdLength: number,
    contentId: number,
    contentIdLength: number,
    width: number,
    height: number,
    rgba: number,
    rgbaLength: number,
    encoded: number,
    encodedLength: number,
  ): number;
  _bb_wasm_session_add_asset_metadata(
    session: number,
    logicalId: number,
    logicalIdLength: number,
    contentId: number,
    contentIdLength: number,
    width: number,
    height: number,
    hasEncodedBytes: number,
  ): number;
  _bb_wasm_session_hydrate_asset(
    session: number,
    logicalId: number,
    logicalIdLength: number,
    rgba: number,
    rgbaLength: number,
    encoded: number,
    encodedLength: number,
  ): number;
  _bb_wasm_session_compile(session: number, source: number, length: number): number;
  _bb_wasm_diagnostic_count(session: number): number;
  _bb_wasm_diagnostic_get(session: number, index: number, info: number): number;
  _bb_wasm_diagnostic_copy_message(session: number, index: number, destination: number, capacity: number): number;
  _bb_wasm_trace_at(session: number, byteOffset: number, info: number): number;
  _bb_wasm_parameter_count(session: number): number;
  _bb_wasm_parameter_get(session: number, index: number, info: number): number;
  _bb_wasm_parameter_copy_name(session: number, index: number, destination: number, capacity: number): number;
  _bb_wasm_parameter_set_bool(session: number, index: number, value: number): number;
  _bb_wasm_parameter_set_integer(session: number, index: number, low: number, high: number): number;
  _bb_wasm_parameter_set_number(session: number, index: number, value: number): number;
  _bb_wasm_parameter_set_color(session: number, index: number, rgba: number): number;
  _bb_wasm_output_count(session: number): number;
  _bb_wasm_output_get(session: number, index: number, info: number): number;
  _bb_wasm_artifact_get(session: number, output: number, low: number, high: number, info: number): number;
  _bb_wasm_artifact_copy_key(session: number, output: number, low: number, high: number, destination: number, capacity: number): number;
  _bb_wasm_artifact_copy_path(session: number, output: number, low: number, high: number, destination: number, capacity: number): number;
  _bb_wasm_graph_node_get(session: number, node: number, info: number): number;
  _bb_wasm_graph_asset_copy_content_id(session: number, node: number, destination: number, capacity: number): number;
  _bb_wasm_graph_gradient_stop_get(session: number, node: number, stop: number, info: number): number;
  _bb_wasm_render_output_rgba(
    session: number,
    output: number,
    low: number,
    high: number,
    destination: number,
    capacity: number,
    info: number,
  ): number;
}

export type BinBlockWasmFactory = (options?: { locateFile?: (path: string) => string }) => Promise<BinBlockWasmModule>;

export interface BinBlockSpan {
  sourceId: number;
  from: number;
  to: number;
}

export interface BinBlockDiagnostic extends BinBlockSpan {
  severity: number;
  code: number;
  message: string;
}

export interface BinBlockTrace extends BinBlockSpan {
  type: number;
  image: number;
  outputIndex?: bigint;
}

export interface BinBlockParameter extends BinBlockSpan {
  index: number;
  name: string;
  type: number;
}

export interface BinBlockOutput extends BinBlockSpan {
  index: number;
  itemType: number;
  cardinality: bigint;
}

export interface BinBlockArtifact extends BinBlockSpan {
  image: number;
  key: string;
  path: string;
  aliasIdentity: number;
}

export interface BinBlockImage {
  width: number;
  height: number;
  pixels: Uint8ClampedArray;
}

export interface BinBlockGradientStop {
  offset: number;
  rgba: number;
  easing: number;
  hasEasing: boolean;
}

export interface BinBlockGraphNode {
  id: number;
  kind: number;
  width: number;
  height: number;
  inputs: readonly number[];
  options: readonly number[];
  scalars: readonly number[];
  stops: readonly BinBlockGradientStop[];
  auxCount: number;
  assetContentId?: string;
}

export interface BinBlockAssetMetadata {
  logicalId: string;
  contentId: string;
  width: number;
  height: number;
  hasEncodedBytes?: boolean;
}

export class BinBlockStatusError extends Error {
  constructor(
    readonly operation: string,
    readonly status: number,
  ) {
    super(`${operation} failed with BinBlock status ${status}.`);
    this.name = 'BinBlockStatusError';
  }
}

const encoder = new TextEncoder();
const decoder = new TextDecoder();
const UINT64_NONE = 0xffff_ffff_ffff_ffffn;

function splitU64(value: bigint): readonly [number, number] {
  const unsigned = BigInt.asUintN(64, value);
  return [Number(unsigned & 0xffff_ffffn), Number(unsigned >> 32n)];
}

function joinU64(low: number, high: number): bigint {
  return BigInt(low >>> 0) | (BigInt(high >>> 0) << 32n);
}

export class BinBlockRuntime {
  readonly #module: BinBlockWasmModule;
  readonly #session: number;
  #requestGeneration = 0;
  #disposed = false;
  readonly #logicalAssetByContentId = new Map<string, string>();
  readonly #hydratedAssetContentIds = new Set<string>();

  constructor(module: BinBlockWasmModule) {
    this.#module = module;
    this.#session = module._bb_wasm_session_create();
    if (this.#session === 0) throw new BinBlockStatusError('session create', 2);
  }

  get generation(): number {
    this.#assertLive();
    return this.#module._bb_wasm_session_generation(this.#session);
  }

  compile(source: string): readonly BinBlockDiagnostic[] {
    this.#assertLive();
    const bytes = encoder.encode(source);
    const pointer = this.#allocate(bytes.length);
    try {
      this.#module.HEAPU8.set(bytes, pointer);
      this.#check('compile', this.#module._bb_wasm_session_compile(this.#session, pointer, bytes.length));
    } finally {
      this.#module._free(pointer);
    }
    this.#requestGeneration += 1;
    return this.diagnostics();
  }

  clearResources(): void {
    this.#assertLive();
    this.#module._bb_wasm_session_clear_resources(this.#session);
    this.#logicalAssetByContentId.clear();
    this.#hydratedAssetContentIds.clear();
    this.#requestGeneration += 1;
  }

  registerModule(specifier: string, identity: string, source: string): void {
    this.#assertLive();
    const values = [encoder.encode(specifier), encoder.encode(identity), encoder.encode(source)] as const;
    const pointers = values.map((value) => this.#copyIntoWasm(value));
    try {
      this.#check(
        'module registration',
        this.#module._bb_wasm_session_add_module(
          this.#session,
          pointers[0],
          values[0].length,
          pointers[1],
          values[1].length,
          pointers[2],
          values[2].length,
        ),
      );
    } finally {
      for (const pointer of pointers) this.#module._free(pointer);
    }
  }

  registerAssetMetadata(metadata: BinBlockAssetMetadata): void {
    this.#assertLive();
    if (!Number.isSafeInteger(metadata.width) || !Number.isSafeInteger(metadata.height) || metadata.width <= 0 || metadata.height <= 0)
      throw new RangeError('Registered asset metadata has invalid dimensions.');
    const logical = encoder.encode(metadata.logicalId);
    const content = encoder.encode(metadata.contentId);
    const pointers = [this.#copyIntoWasm(logical), this.#copyIntoWasm(content)];
    try {
      this.#check(
        'asset metadata registration',
        this.#module._bb_wasm_session_add_asset_metadata(
          this.#session,
          pointers[0],
          logical.length,
          pointers[1],
          content.length,
          metadata.width,
          metadata.height,
          Number(metadata.hasEncodedBytes ?? false),
        ),
      );
      if (!this.#logicalAssetByContentId.has(metadata.contentId)) this.#logicalAssetByContentId.set(metadata.contentId, metadata.logicalId);
    } finally {
      for (const pointer of pointers) this.#module._free(pointer);
    }
  }

  hydrateAsset(contentId: string, image: BinBlockImage, encoded?: Uint8Array): void {
    this.#assertLive();
    const logicalId = this.#logicalAssetByContentId.get(contentId);
    if (logicalId === undefined) throw new ReferenceError(`No registered metadata has content identity ${contentId}.`);
    if (image.width <= 0 || image.height <= 0 || image.pixels.length !== image.width * image.height * 4)
      throw new RangeError('Hydrated asset has invalid RGBA dimensions.');
    const logical = encoder.encode(logicalId);
    const encodedBytes = encoded ?? new Uint8Array();
    const pointers = [this.#copyIntoWasm(logical), this.#copyIntoWasm(image.pixels), this.#copyIntoWasm(encodedBytes)];
    try {
      this.#check(
        'asset hydration',
        this.#module._bb_wasm_session_hydrate_asset(
          this.#session,
          pointers[0],
          logical.length,
          pointers[1],
          image.pixels.length,
          encodedBytes.length === 0 ? 0 : pointers[2],
          encodedBytes.length,
        ),
      );
      this.#hydratedAssetContentIds.add(contentId);
    } finally {
      for (const pointer of pointers) this.#module._free(pointer);
    }
  }

  assetIsHydrated(contentId: string): boolean {
    this.#assertLive();
    return this.#hydratedAssetContentIds.has(contentId);
  }

  registerAsset(logicalId: string, contentId: string, image: BinBlockImage, encoded?: Uint8Array): void {
    this.#assertLive();
    if (image.width <= 0 || image.height <= 0 || image.pixels.length !== image.width * image.height * 4)
      throw new RangeError('Registered asset has invalid RGBA dimensions.');
    const logical = encoder.encode(logicalId);
    const content = encoder.encode(contentId);
    const encodedBytes = encoded ?? new Uint8Array();
    const pointers = [
      this.#copyIntoWasm(logical),
      this.#copyIntoWasm(content),
      this.#copyIntoWasm(image.pixels),
      this.#copyIntoWasm(encodedBytes),
    ];
    try {
      this.#check(
        'asset registration',
        this.#module._bb_wasm_session_add_asset_rgba(
          this.#session,
          pointers[0],
          logical.length,
          pointers[1],
          content.length,
          image.width,
          image.height,
          pointers[2],
          image.pixels.length,
          encodedBytes.length === 0 ? 0 : pointers[3],
          encodedBytes.length,
        ),
      );
      if (!this.#logicalAssetByContentId.has(contentId)) this.#logicalAssetByContentId.set(contentId, logicalId);
      this.#hydratedAssetContentIds.add(contentId);
    } finally {
      for (const pointer of pointers) this.#module._free(pointer);
    }
  }

  diagnostics(): readonly BinBlockDiagnostic[] {
    this.#assertLive();
    const count = this.#module._bb_wasm_diagnostic_count(this.#session);
    const info = this.#allocate(28);
    const diagnostics: BinBlockDiagnostic[] = [];
    try {
      for (let index = 0; index < count; index += 1) {
        this.#writeU32(info, 28);
        this.#check('diagnostic query', this.#module._bb_wasm_diagnostic_get(this.#session, index, info));
        const fields = this.#u32(info, 7);
        diagnostics.push({
          severity: fields[1],
          code: fields[2],
          sourceId: fields[3],
          from: fields[4],
          to: fields[5],
          message: this.#copyString(fields[6], (destination, capacity) =>
            this.#module._bb_wasm_diagnostic_copy_message(this.#session, index, destination, capacity),
          ),
        });
      }
    } finally {
      this.#module._free(info);
    }
    return diagnostics;
  }

  traceAt(byteOffset: number): BinBlockTrace | undefined {
    this.#assertLive();
    const info = this.#allocate(32);
    try {
      this.#writeU32(info, 32);
      const status = this.#module._bb_wasm_trace_at(this.#session, byteOffset, info);
      if (status === 8) return undefined;
      this.#check('semantic trace query', status);
      const fields = this.#u32(info, 8);
      const outputIndex = joinU64(fields[3], fields[4]);
      return {
        type: fields[1],
        image: fields[2],
        ...(outputIndex === UINT64_NONE ? {} : { outputIndex }),
        sourceId: fields[5],
        from: fields[6],
        to: fields[7],
      };
    } finally {
      this.#module._free(info);
    }
  }

  parameters(): readonly BinBlockParameter[] {
    this.#assertLive();
    const count = this.#module._bb_wasm_parameter_count(this.#session);
    const info = this.#allocate(24);
    const parameters: BinBlockParameter[] = [];
    try {
      for (let index = 0; index < count; index += 1) {
        this.#writeU32(info, 24);
        this.#check('parameter query', this.#module._bb_wasm_parameter_get(this.#session, index, info));
        const fields = this.#u32(info, 6);
        parameters.push({
          index,
          type: fields[1],
          name: this.#copyString(fields[2], (destination, capacity) =>
            this.#module._bb_wasm_parameter_copy_name(this.#session, index, destination, capacity),
          ),
          sourceId: fields[3],
          from: fields[4],
          to: fields[5],
        });
      }
    } finally {
      this.#module._free(info);
    }
    return parameters;
  }

  setParameter(index: number, type: number, value: boolean | bigint | number | string): readonly BinBlockDiagnostic[] {
    this.#assertLive();
    let status: number;
    if (type === 1 && typeof value === 'boolean') {
      status = this.#module._bb_wasm_parameter_set_bool(this.#session, index, Number(value));
    } else if (type === 2 && typeof value === 'bigint') {
      const [low, high] = splitU64(value);
      status = this.#module._bb_wasm_parameter_set_integer(this.#session, index, low, high);
    } else if ((type === 3 || type === 8 || type === 9) && typeof value === 'number') {
      status = this.#module._bb_wasm_parameter_set_number(this.#session, index, value);
    } else if (type === 5 && typeof value === 'string' && /^#[\da-f]{8}$/i.test(value)) {
      status = this.#module._bb_wasm_parameter_set_color(this.#session, index, Number.parseInt(value.slice(1), 16));
    } else {
      throw new TypeError('Parameter value does not match its BinScript semantic type.');
    }
    this.#check('parameter update', status);
    this.#requestGeneration += 1;
    return this.diagnostics();
  }

  outputs(): readonly BinBlockOutput[] {
    this.#assertLive();
    const count = this.#module._bb_wasm_output_count(this.#session);
    const info = this.#allocate(28);
    const outputs: BinBlockOutput[] = [];
    try {
      for (let index = 0; index < count; index += 1) {
        this.#writeU32(info, 28);
        this.#check('output query', this.#module._bb_wasm_output_get(this.#session, index, info));
        const fields = this.#u32(info, 7);
        outputs.push({
          index,
          itemType: fields[1],
          cardinality: joinU64(fields[2], fields[3]),
          sourceId: fields[4],
          from: fields[5],
          to: fields[6],
        });
      }
    } finally {
      this.#module._free(info);
    }
    return outputs;
  }

  artifacts(output: number, start: bigint, count: number): readonly BinBlockArtifact[] {
    this.#assertLive();
    if (!Number.isSafeInteger(count) || count < 0 || count > 10_000) throw new RangeError('Artifact slice is not bounded.');
    const info = this.#allocate(36);
    const artifacts: BinBlockArtifact[] = [];
    try {
      for (let offset = 0; offset < count; offset += 1) {
        const index = start + BigInt(offset);
        const [low, high] = splitU64(index);
        this.#writeU32(info, 36);
        this.#check('artifact query', this.#module._bb_wasm_artifact_get(this.#session, output, low, high, info));
        const fields = this.#u32(info, 9);
        artifacts.push({
          image: fields[1],
          key: this.#copyString(fields[2], (destination, capacity) =>
            this.#module._bb_wasm_artifact_copy_key(this.#session, output, low, high, destination, capacity),
          ),
          path: this.#copyString(fields[3], (destination, capacity) =>
            this.#module._bb_wasm_artifact_copy_path(this.#session, output, low, high, destination, capacity),
          ),
          aliasIdentity: fields[4],
          sourceId: fields[6],
          from: fields[7],
          to: fields[8],
        });
      }
    } finally {
      this.#module._free(info);
    }
    return artifacts;
  }

  graphNode(node: number): BinBlockGraphNode {
    this.#assertLive();
    const info = this.#allocate(108);
    try {
      this.#writeU32(info, 108);
      this.#check('graph node query', this.#module._bb_wasm_graph_node_get(this.#session, node, info));
      const fields = this.#u32(info, 27);
      const scalarView = new DataView(this.#module.HEAPU8.buffer, info + 52, 48);
      const scalars = Array.from({ length: 6 }, (_, index) => scalarView.getFloat64(index * 8, true));
      const stopCount = fields[25];
      const assetContentIdLength = fields[26];
      const stops: BinBlockGradientStop[] = [];
      const stopInfo = this.#allocate(24);
      try {
        for (let index = 0; index < stopCount && (fields[1] === 4 || fields[1] === 5); index += 1) {
          this.#writeU32(stopInfo, 24);
          this.#check('gradient stop query', this.#module._bb_wasm_graph_gradient_stop_get(this.#session, node, index, stopInfo));
          const stopFields = this.#u32(stopInfo, 6);
          stops.push({
            offset: new DataView(this.#module.HEAPU8.buffer, stopInfo + 4, 8).getFloat64(0, true),
            rgba: stopFields[3],
            easing: stopFields[4],
            hasEasing: stopFields[5] !== 0,
          });
        }
      } finally {
        this.#module._free(stopInfo);
      }
      return {
        id: node,
        kind: fields[1],
        width: fields[2],
        height: fields[3],
        inputs: Array.from(fields.slice(5, 5 + fields[4])),
        options: Array.from(fields.slice(7, 13)),
        scalars,
        stops,
        auxCount: stopCount,
        ...(assetContentIdLength === 0
          ? {}
          : {
              assetContentId: this.#copyString(assetContentIdLength, (destination, capacity) =>
                this.#module._bb_wasm_graph_asset_copy_content_id(this.#session, node, destination, capacity),
              ),
            }),
      };
    } finally {
      this.#module._free(info);
    }
  }

  requiredAssetContentIds(root: number): readonly string[] {
    this.#assertLive();
    const visited = new Set<number>();
    const contentIds = new Set<string>();
    const pending = [root];
    while (pending.length !== 0) {
      const node = pending.pop();
      if (node === undefined || visited.has(node)) continue;
      visited.add(node);
      const info = this.graphNode(node);
      if (info.assetContentId !== undefined) contentIds.add(info.assetContentId);
      pending.push(...info.inputs);
    }
    return [...contentIds];
  }

  async render(output: number, item: bigint): Promise<BinBlockImage> {
    this.#assertLive();
    const request = ++this.#requestGeneration;
    const compiledGeneration = this.generation;
    await Promise.resolve();
    if (request !== this.#requestGeneration || compiledGeneration !== this.generation) throw new DOMException('Superseded', 'AbortError');
    const [low, high] = splitU64(item);
    const info = this.#allocate(16);
    try {
      this.#writeU32(info, 16);
      this.#check('render size query', this.#module._bb_wasm_render_output_rgba(this.#session, output, low, high, 0, 0, info));
      const dimensions = this.#u32(info, 4);
      const pixels = this.#allocate(dimensions[3]);
      try {
        this.#writeU32(info, 16);
        this.#check('render', this.#module._bb_wasm_render_output_rgba(this.#session, output, low, high, pixels, dimensions[3], info));
        if (request !== this.#requestGeneration || compiledGeneration !== this.generation)
          throw new DOMException('Superseded', 'AbortError');
        return {
          width: dimensions[1],
          height: dimensions[2],
          pixels: new Uint8ClampedArray(this.#module.HEAPU8.slice(pixels, pixels + dimensions[3])),
        };
      } finally {
        this.#module._free(pixels);
      }
    } finally {
      this.#module._free(info);
    }
  }

  supersede(): void {
    this.#requestGeneration += 1;
  }

  dispose(): void {
    if (this.#disposed) return;
    this.#module._bb_wasm_session_destroy(this.#session);
    this.#disposed = true;
    this.#requestGeneration += 1;
  }

  #allocate(size: number): number {
    const pointer = this.#module._malloc(Math.max(1, size));
    if (pointer === 0) throw new BinBlockStatusError('Wasm allocation', 2);
    return pointer;
  }

  #copyIntoWasm(bytes: Uint8Array | Uint8ClampedArray): number {
    const pointer = this.#allocate(bytes.length);
    this.#module.HEAPU8.set(bytes, pointer);
    return pointer;
  }

  #copyString(length: number, copy: (destination: number, capacity: number) => number): string {
    const pointer = this.#allocate(length);
    try {
      this.#check('string copy', copy(pointer, length));
      return decoder.decode(this.#module.HEAPU8.subarray(pointer, pointer + length));
    } finally {
      this.#module._free(pointer);
    }
  }

  #writeU32(pointer: number, value: number): void {
    this.#module.HEAPU32[pointer >>> 2] = value;
  }

  #u32(pointer: number, count: number): Uint32Array {
    return this.#module.HEAPU32.slice(pointer >>> 2, (pointer >>> 2) + count);
  }

  #check(operation: string, status: number): void {
    if (status !== 0) throw new BinBlockStatusError(operation, status);
  }

  #assertLive(): void {
    if (this.#disposed) throw new ReferenceError('BinBlock runtime has been disposed.');
  }
}

export async function createBinBlockRuntime(
  factory: BinBlockWasmFactory,
  options?: { locateFile?: (path: string) => string },
): Promise<BinBlockRuntime> {
  return new BinBlockRuntime(await factory(options));
}
