# ADR 0001: Initial values and types

Status: accepted

## Decision

The semantic layer uses tagged values, never strings or JSON as a universal value
representation. The initial public value set is `bool`, signed 64-bit integer,
finite binary64 number, UTF-8 string, `RGBA8` color, two-dimensional finite
vector, degrees, percentage, image node, logical asset reference,
callable handle, and output artifact. An ordered `collection<T>` is a lazy plan
over values rather than a value encoded inside another value.

Percentages retain their source-scale value (`50%` is stored as `50.0`); APIs that
need a unit interval convert explicitly. Degrees retain degrees. Unit-bearing
values do not implicitly convert to unitless numbers. The only initial implicit
numeric conversion is lossless integer-to-number widening where a function
signature permits it. Strings, symbols, assets, colors, and numbers do not
implicitly convert between one another.

Colors preserve all four channels, including RGB when alpha is zero. Strings and
artifact/asset identifiers are owned by base collection plans and validated
according to the context UTF-8 policy. Direct-C callback results remain borrowed
under the callback lifetime contract and are validated before publication.

The compiler will enforce homogeneous `collection<T>` types. The low-level C
planner also supports fixed-width rows created by `zip` and `product`; every row
element remains individually tagged and validated.

## Consequences

Type errors are reported before render planning. Asset resolution and callable
dispatch use stable logical handles rather than host pointers. Future serialized
forms must define numeric representation and endianness explicitly instead of
copying native unions.
