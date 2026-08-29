# TypeScript behavioral oracle

This directory preserves the original BinScript parser, JSON recipe model,
eager executor, raster recovery code, and notebook adapter for differential and
historical tests.

It is deliberately outside `src/`: the production application bundles only the
CodeMirror host, the JavaScript Wasm wrapper, the WebGL2 lowering adapter, and
`libbinblock` compiled to Wasm. Nothing in this directory defines production
language, collection, graph, or pixel semantics.
