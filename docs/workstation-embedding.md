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

## Publishing a prepared immutable version

`DataService::importPreparedFile` accepts a `ParsedFile` already normalized by a
trusted host format adapter. Pass the currently readable file, its original
identity, the source reference that will be valid after publication, and the
expected full SHA-256. The reference may be relative to the data project for a
relocatable managed source. This method neither copies nor rewrites the source.

The service checks the source digest before storing and again before commit.
Objects and the source record are committed together; shape errors, cancellation
and content drift roll back both. It returns a database-local source ID. The host
must publish the resulting database with its own version journal and retain the
explicit source/object mapping. The API does not make a second writable catalog
or silently coordinate transactions across independently writable databases.

Prepared curve values must already contain NaN for source-declared missing cells.
Finite negative values, including -999 and -99999, are retained. Ordinary file
imports keep their existing parsing and null-value rules; `ParseOptions` has no
host-only switch. Workstation-specific columns may use the public `SourceTable`
store alongside the typed objects to preserve fields not yet in the shared model.

Close the writer before hashing/publishing its SQLite file, then use read-only
sessions on consuming threads. A host using immutable versions should create a
new prepared model for edits and reimports, rather than mutate a published model
or ask the ordinary parser to reconstruct its host-specific interpretation.
