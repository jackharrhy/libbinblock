# TypeScript behavioral oracle

This directory preserves the original BinScript parser, JSON recipe model,
eager executor, raster recovery code, and notebook adapter for differential and
historical tests.

It is deliberately isolated from `apps/browser/`: the production application
bundles only the CodeMirror host, the JavaScript Wasm wrapper, the WebGL2
lowering adapter, and `libbinblock` compiled to Wasm. Nothing in this directory
defines production language, collection, graph, or pixel semantics.

The matching historical tests live in `test/`, and the retired JSON recipe
model is documented in [RECIPES.md](RECIPES.md).
