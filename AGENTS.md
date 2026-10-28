# Standalone data-service prototype

Read `docs/requirements.md` and the public headers before implementation. Use C++20, CMake and Qt6 internally; public data objects use standard C++ types.

Keep the layers separate: `src/parsers` must not import SQL or UI. `src/flow` handles import and CRUD through `src/store`. `src/services` composes business queries; `src/adapters` accesses the seismic engine. `demo` calls the public service only. Keep QtWidgets inside `demo` and GUI tests. The project must remain independently buildable.

All input paths and public strings are UTF-8. Preserve raw values and source provenance. Do not guess unconfirmed units or coordinates. Source-declared null values take precedence; negative values are not universally invalid. Publish imports transactionally per file, skip duplicates, and expose failures. Large SEG-Y files remain external and read-only.

Directory workers prepare files without SQL. Publish in the original sorted order on the coordinator thread. Invoke caller-provided progress/cancellation callbacks only from that thread. Count both running and completed-but-unpublished tasks in the staging budget; join workers on cancellation or failure.

Build with at most 8 workers. Tests must verify values, rollback, reopening, edits, import consistency and cancellation, not just class existence. Generate synthetic fixtures in temporary directories. Optional external-data checks must accept user-supplied paths and make no assumptions about a private dataset.

Do not commit data files, generated databases, results, screenshots, build products, credentials or machine-specific source paths. Keep private validation outside this repository. Preserve third-party notices and document local vendor patches.
