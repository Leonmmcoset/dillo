# Pinned JavaScript dependencies

- QuickJS 2026-06-04, MIT: `quickjs/LICENSE` and `quickjs/UPSTREAM`.
- Lexbor 3.0.0, Apache-2.0: `lexbor/LICENSE`, `lexbor/NOTICE` and `lexbor/UPSTREAM`.

UPSTREAM files record the original archive URL and SHA-256. Source files are
compiled into private static libraries only when JavaScript is enabled. No
QuickJS command-line interpreter, libc module, filesystem or process bindings
are linked into Dillo. Keep upstream licenses and notices in release archives.
The Linux Pango/Cairo text backend uses system libraries; it is not vendored.
QuickJS's UPSTREAM file also records the local unsigned-shift fix used by its
bytecode optimizer.
