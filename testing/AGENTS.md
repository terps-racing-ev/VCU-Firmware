New `tests/<dir>/test_<name>.c` files must include `unity.h`, the module header,
required standard/mock headers, and empty `void setUp(void)` / `void tearDown(void)` stubs.
Add test cases only when requested.
It auto-links `Core/Src/<dir>/<name>.c`; discovery is automatic.
For dependencies, add sibling `test_<name>.mk`:
`MOCKS_<dir>/<name> := dependency` and include `Mockdependency.h` in the test.
CMock regenerates mocks from headers into `build/mocks/`; don't edit generated mocks.
Use `EXTRA_SOURCES_<dir>/<name>` for other sources (paths relative to `testing/`).
Override `SOURCES_<dir>/<name>` or `CPPFLAGS_<dir>/<name>` there if needed.
Include mock headers in the test; Unity manages their lifecycle.
From repo root: `make test` (all) or `make test <dir>/<name>` (selected).
HAL/RTOS host declarations live in `support/`; extend them for new APIs.
For other external headers, set `MOCK_HEADER_<name> := path/header.h` in the `.mk`.
Flash drivers need a mapped-memory/linker fixture before testing direct flash access.
Requires host GCC (or RubyInstaller DevKit), Ruby, GNU Make; override `CC`/`RUBY` as needed.
