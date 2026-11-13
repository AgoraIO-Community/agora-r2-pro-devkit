# AOSL

AOSL provides portable threads, queued work, I/O, networking, logging and memory services.
Public API headers live in `include/api/`; HAL interfaces live in `platform/include/hal/`.

This committed source snapshot retains the shared runtime and Linux HAL at the revision
recorded in [dependencies.lock.json](../../dependencies.lock.json). Non-Linux ports,
repository/editor metadata and the standalone test entry point are intentionally omitted.
The complete [LICENSE](LICENSE), including its additional conditions, remains applicable.

## Application integration

The parent [SDK integration](../../cmake/dependencies/sdk.cmake) fixes `CONFIG_PLATFORM=linux`
and sets `AOSL_DIR` to this sibling directory before adding MyBot. MyBot includes AOSL's CMake
inputs with `AOSL_DECLARE_PROJECT=OFF` and `AOSL_COMPILE_TEST=OFF`, then creates the single
`aosl` static-library target from `AOSL_ADD_SOURCES` and the exported include/options lists.
No second AOSL target is created by the parent application.

`test/aosl_test.c` remains because `cmake/build.cmake` unconditionally compiles it into the
library. It is distinct from the omitted standalone executable entry point.

## Snapshot layout

```text
aosl/
├── include/                # public API and shared internal headers
├── kernel/                 # work queues, timers, files and threading
├── lib/                    # common runtime implementations
├── mm/                     # memory allocation
├── net/                    # networking and DNS
├── platform/include/       # portable HAL interfaces
├── platform/src/linux/     # complete Linux HAL and configuration
├── cmake/                  # upstream build inputs
└── test/aosl_test.c         # source included in the production library
```

Build the complete application from its [project entry point](../../README.md).
A standalone Linux library build must explicitly disable the omitted test executable:

```sh
cmake -S third_party/aosl -B build/aosl -DCONFIG_PLATFORM=linux -DAOSL_COMPILE_TEST=OFF
cmake --build build/aosl
```

Keep all runtime source, CMake and license changes deliberate and synchronized with the
dependency lock and provenance records. See the [dependency workflow](../../docs/development/dependencies.zh-CN.md).
