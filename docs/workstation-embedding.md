# Embedding in a workstation

The library remains a standalone survey-data service. It can also be built using
`add_subdirectory`; `PDS_BUILD_DEMO`, `PDS_BUILD_TOOLS`, `PDS_BUILD_EXAMPLES`,
`PDS_BUILD_TESTS` and `PDS_INSTALL` default to the top-level-project setting.
They can be selected explicitly. With tests and demo disabled Qt Widgets is not
a required component of the library.

A host using the same headless seismic engine can pass
`PDS_SEISMIC_ENGINE_TARGET` and `PDS_SEISMIC_INCLUDE_DIR`. Both must refer to a
compatible target/header set. The service then does not build or link its own
copy of that engine. SDK installation requires the standalone engine instead.
The copied ZIP code is isolated in `paleo::data::zip`; host ZIP symbols cannot
silently override its implementation.

`paleo_data/catalog.h` supplies standard C++ entity, asset, immutable-version and
explicit-link records. `CatalogStorage` borrows a named, already-open QSQLITE
connection on the calling thread. The host owns connection lifetime, transactions,
schema-version gates, backups and recovery. Schema DDL is explicit, not run by
the constructor. Storage never commits, reopens or changes the access mode of
the host connection. JSON extension columns are preserved verbatim. File/path
containment and domain validation remain the host's responsibility.

These records complement the native `DataService` survey objects. They do not
automatically mirror or reconcile the two models. In particular native curve
updates are in-place; applications needing immutable versions must first define
an explicit version/source mapping and a coordinated commit protocol.

`DataService::openProjectReadOnly` reads an existing project without schema
upgrades. Import and reimport fail before staging or copying sources; edits fail
on the SQLite read-only connection. Use a new instance on every thread. SQLite
WAL readers may still use sidecar files; this is not an immutable snapshot mode.

Tests generate temporary inputs and cover reopen, rollback, access mode, UTF-8,
unknown catalog metadata and rejected mutations. No workstation dependency or
private project is needed.
