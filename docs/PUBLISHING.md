# Source-only repository

This repository contains implementation source, public headers, source-based tests,
build and packaging recipes, documentation, and the interactive model reference.
No input dataset, project database, extracted measurement, application screenshot,
runtime package, compiled library, or acceptance output is included.

Tests generate small synthetic fixtures in temporary directories at runtime. Their
well names and numeric values are illustrative. Optional local acceptance tools can
inspect caller-supplied files, but their outputs must remain outside version control.
Directory-specific private acceptance assertions have been replaced by generic shape
and consistency checks. Runtime APIs and parser behavior remain unchanged.

The following local-only content is excluded:

- Project folders, source copies, exports, temporary folders, and SQLite files.
- SEG-Y, LAS, spreadsheet, image, video, PDF, archive, and other input-data files.
- Validation JSON, extracted source cells/curves, screenshots, logs, and private paths.
- Build outputs, installed SDK archives, runtime bundles, and generated examples.
- Upstream FreeXL binary test fixtures, decorative images, and the redundant source tarball.

Third-party source files retain their copyright and license notices. Do not apply a
blanket license that overrides vendored code. See each vendor directory for its origin,
version, patch notes, and license terms. FreeXL's binary upstream fixtures are omitted;
the data-service regression tests generate their own synthetic inputs instead.
