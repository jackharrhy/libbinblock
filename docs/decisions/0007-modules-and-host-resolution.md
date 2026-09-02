# ADR 0007: Modules, assets, and host resolution

Status: accepted

## Decision

Imports and assets are logical identities. The compiler asks host callbacks to
resolve a module request or asset request and never derives a filesystem path.
A resolved module is built-in or UTF-8 source. It carries a stable content identity used for import-depth
checks, cycle diagnostics, and cache identity.

A resolved asset carries a logical ID, content ID, dimensions, format
constraints, and availability metadata. Decoded RGBA8 and encoded bytes use
separate callbacks because decoded-pixel identity and byte identity are separate
contracts. Hosts may register metadata first and hydrate payloads later; borrowed
callback memory is consumed only for the documented callback/query lifetime.

`binblock/basic` is a built-in module containing only generic language and image
operations. Reference compatibility mappings and assets are isolated in
`binblock/reference-set` and the generated conformance program. Native tests may
resolve from memory and the CLI may use files. The normal browser application
uses an asset-free generated set and does not package the reference corpus;
another browser host may still provide verified HTTP assets. Godot may use
resources, and Wii may use packaged source bytes without changing the compiler
or graph.

## Consequences

Core headers contain no filesystem, network, archive, browser, engine, or console
types. Content identity rather than a host path drives graph hashes and hydration
caches. Module and asset cycles are structured diagnostics. Permission and
not-found failures remain host-resolver results with source-ranged compiler
context.
