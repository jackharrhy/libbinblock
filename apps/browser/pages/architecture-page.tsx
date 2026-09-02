import { graphNodeKinds, targetDescriptions } from '../project-content.js';

const compileCode = `bb_context_add_source(context, name, source, &source_id);
bb_syntax_parse(context, source_id, &syntax);
bb_program_compile_with_options(context, syntax, &options, &program);
bb_syntax_tree_destroy(syntax);`;

const programFields = `struct bb_program {
  bb_context *context;
  bb_limits limits;
  bb_compile_options options;
  bb_image_graph *graph;
  bb_diagnostic_store diagnostics;
  bb_program_binding *bindings;
  bb_program_module_record *modules;
  bb_semantic_trace *traces;
  bb_program_output_record *outputs;
};`;

const renderCode = `needed[root - 1] = 1;
stack[stack_count++] = root;
while (stack_count != 0) {
  const bb_image_node current = stack[--stack_count];
  bb_graph_push_children(
    bb_image_graph_get_node(graph, current),
    needed, stack, &stack_count
  );
}

for (index = 0; index < graph->node_count; index += 1) {
  if (!needed[index]) continue;
  bb_image_graph_node_dimensions(
    graph, (bb_image_node)(index + 1), &width, &height
  );
  node_work = (uint64_t)width * (uint64_t)height;
  if (UINT64_MAX - work_units < node_work ||
      work_units + node_work > effective_options.max_work_units)
    return BB_STATUS_LIMIT_EXCEEDED;
  work_units += node_work;
}

for (index = 0; index < graph->node_count; index += 1) {
  if (!needed[index]) continue;
  bb_graph_render_node(
    context, &graph->nodes[index], surfaces,
    decode, decode_user, &surfaces[index + 1]
  );
}`;

export function ArchitecturePage() {
  return (
    <article className="technical-page page-width">
      <header className="page-header">
        <h1>Implementation</h1>
        <p>How the parser, compiler, graph, and renderer turn source into pixels. Host adapters take it from there.</p>
      </header>

      <div className="technical-layout">
        <nav className="page-index" aria-label="Implementation sections">
          <a href="#syntax">Syntax</a>
          <a href="#compiler">Compiler</a>
          <a href="#graph">Image graph</a>
          <a href="#collections">Collections</a>
          <a href="#raster">Raster</a>
          <a href="#hosts">Hosts</a>
        </nav>

        <div className="technical-sections">
          <section id="syntax">
            <h2>Syntax and source text</h2>
            <p>
              <code>bb_syntax_parse</code> reads source stored in <code>bb_context</code>. It keeps whitespace and comments in the token
              stream, so tools can inspect the original text. A syntax error adds a diagnostic and an error node without ending the parse.
            </p>
            <dl className="implementation-facts">
              <div>
                <dt>Token kinds</dt>
                <dd>Whitespace, comment, identifier, number, color, string, symbol, error, EOF</dd>
              </div>
              <div>
                <dt>Node kinds</dt>
                <dd>Program, import, binding, expression, literals, arrays, calls, member calls, named arguments, gradient stops</dd>
              </div>
              <div>
                <dt>Locations</dt>
                <dd>
                  Every token, node, diagnostic, and semantic trace uses a <code>bb_span</code> with source ID and byte offsets.
                </dd>
              </div>
              <div>
                <dt>Limits</dt>
                <dd>The parser checks nesting depth and records diagnostic code 1107 when it is exceeded.</dd>
              </div>
            </dl>
            <p className="file-reference">
              <code>include/binblock/syntax.h</code>
              <code>lib/frontend/syntax.c</code>
            </p>
          </section>

          <section id="compiler">
            <h2>Compiling a program</h2>
            <p>
              Compilation resolves imports, registers bindings, evaluates typed expressions, and publishes standalone image or collection
              expressions as outputs. The syntax tree can be destroyed after compilation because the program owns the data it needs.
            </p>
            <div className="code-pair">
              <pre>
                <code>{compileCode}</code>
              </pre>
              <pre>
                <code>{programFields}</code>
              </pre>
            </div>
            <p>
              <code>bb_compile_options</code> is the host boundary. Callbacks resolve modules and asset identities, decode pixels, or return
              original encoded bytes. Parameter overrides replace scalar source bindings with the same type, then the program is compiled
              again so graph shape can change safely.
            </p>
            <p className="file-reference">
              <code>lib/semantic/program_compile.c</code>
              <code>lib/semantic/program_evaluate.c</code>
              <code>lib/semantic/program_members.c</code>
            </p>
          </section>

          <section id="graph">
            <h2>The image graph</h2>
            <p>
              <code>bb_image_node</code> is a 32-bit index. Zero means no node. Each stored node has a kind, structural 128-bit hash, depth,
              options, and up to two input handles. Source provenance is stored beside the nodes so it does not affect structural identity.
            </p>
            <div className="graph-details">
              <div>
                <h3>Node publication</h3>
                <p>
                  A constructor validates dimensions and options, hashes the node kind and child hashes, then compares the candidate with
                  existing nodes. An exact match returns the existing handle. New nodes append in dependency order.
                </p>
                <p>
                  Context limits cap graph depth and node count. <code>bb_image_graph_seal</code> prevents any later constructors from
                  publishing nodes.
                </p>
              </div>
              <div>
                <h3>18 node kinds</h3>
                <ul className="node-kind-list">
                  {graphNodeKinds.map((kind) => (
                    <li key={kind}>{kind}</li>
                  ))}
                </ul>
              </div>
            </div>
            <p className="file-reference">
              <code>include/binblock/graph.h</code>
              <code>lib/core/graph.c</code>
            </p>
          </section>

          <section id="collections">
            <h2>Lazy collections</h2>
            <p>
              A collection is a reference-counted plan node with a kind, depth, row width, and child collections. Values are copied into
              owned storage at the leaves. Derived plans retain their inputs and keep callback state owned by the compiled program.
            </p>
            <dl className="collection-operations">
              <div>
                <dt>Direct index math</dt>
                <dd>
                  <code>map</code>, <code>concat</code>, <code>zip</code>, <code>product</code>, and <code>slice</code> route an index into
                  their child plans. Product order is left-major.
                </dd>
              </div>
              <div>
                <dt>Scanning plans</dt>
                <dd>
                  <code>filter</code>, <code>flat_map</code>, and <code>select_key</code> scan child items with one scratch row. They do not
                  retain a materialized result array.
                </dd>
              </div>
              <div>
                <dt>Cardinality</dt>
                <dd>
                  Counts use <code>uint64_t</code>, checked addition and multiplication, collection depth limits, and the context output
                  limit.
                </dd>
              </div>
              <div>
                <dt>Artifact row</dt>
                <dd>The final row contains a key, path, image-node handle, alias identity, alias target, and provenance span.</dd>
              </div>
            </dl>
            <p className="file-reference">
              <code>include/binblock/collection.h</code>
              <code>lib/core/collection.c</code>
            </p>
          </section>

          <section id="raster">
            <h2>Rendering one output</h2>
            <p>
              Rendering starts from one artifact root. A stack marks its transitive inputs. The renderer sums the pixel area of those nodes
              before allocating surfaces and returns <code>BB_STATUS_LIMIT_EXCEEDED</code> when the configured work limit would be crossed.
            </p>
            <pre>
              <code>{renderCode}</code>
            </pre>
            <p>
              Needed nodes are evaluated in insertion order, which is already dependency order. Each operation calls a concrete CPU raster
              function such as <code>bb_raster_source_over</code>, <code>bb_raster_apply_mask</code>, or{' '}
              <code>bb_raster_resize_lanczos3</code>. Cancellation is checked before work starts, between nodes, and before returning the
              root surface.
            </p>
            <p>
              The canonical surface contract is <code>RGBA8_UNORM</code>, numeric sRGB, and straight alpha. Asset nodes call the host decode
              callback by content ID and verify the returned dimensions.
            </p>
            <p className="file-reference">
              <code>lib/raster/graph_render.c</code>
              <code>lib/raster/*.c</code>
            </p>
          </section>

          <section id="hosts">
            <h2>Host adapters</h2>
            <div className="host-implementation-list">
              {targetDescriptions.map((target) => (
                <article key={target.name}>
                  <h3>{target.name}</h3>
                  <code>{target.path}</code>
                  <p>{target.description}</p>
                </article>
              ))}
            </div>
            <div className="host-notes">
              <article>
                <h3>WebAssembly sessions</h3>
                <p>
                  The Wasm layer has 32 session slots. A handle packs a slot and epoch so stale handles fail lookup. Recompilation
                  increments a generation counter. Modules and assets are copied into session-owned memory, and reference images can
                  register metadata before their RGBA bytes are hydrated. The editor stages the reference PNGs as one packed file, then
                  hydrates and renders them in small batches when the full gallery is requested.
                </p>
              </article>
              <article>
                <h3>Godot resources</h3>
                <p>
                  <code>BinProgram</code> maps scalar bindings to dynamic <code>parameters/</code> properties. Changing one recompiles with
                  typed overrides. Rendering copies the C surface into <code>PackedByteArray</code>, then calls{' '}
                  <code>Image::create_from_data</code> and optionally <code>ImageTexture::create_from_image</code>.
                </p>
              </article>
              <article>
                <h3>Wii textures</h3>
                <p>
                  The Wii adapter accepts the host allocator, measures a 4x4-padded texture, renders one output, and writes GX RGBA8 tiles.
                  Each 64-byte tile stores 16 AR pairs followed by 16 GB pairs. The caller owns the scratch buffer and upload callback.
                </p>
              </article>
            </div>
          </section>
        </div>
      </div>
    </article>
  );
}
