# RocheDB C++ Driver

C++17 RAII wrapper for RocheDB through the stable C ABI.

This repository is the generic OSS C++ driver. Unreal-specific module packaging,
Blueprint bindings, editor tooling, and engine lifecycle integration should live
in a separate Unreal plugin.

## Status

- package version: `0.1.0`
- license: Apache-2.0
- mode: header-only C++ wrapper over `librochedb.so`
- core ABI: RocheDB C ABI version `1`

Implemented:

- embedded `open` / `openDir`
- authenticated cluster `connectAuth`
- `put` / `putVec`
- `get` / `batchGet`
- selection `query`
- vector `retrieve`
- `atlas`
- `locate` / `nextVisit` / `nextJoin`
- ring and galaxy descriptions

Planned:

- native TCP driver path
- package publishing workflow
- broader compatibility matrix

## Case Study Direction

One practical C++ use case is semi-durable structured local data: game settings, save metadata, player profiles, NPC memory, faction history, regional events, mod configuration, local catalogs, and AI/game context stores.

For simulation games, RocheDB can act as a local context store for world, NPC, faction, event, and memory data that accumulates over time and should be retrieved selectively. Applications can still decide how much transient state belongs in RocheDB; the driver does not force a state-management pattern.

## Requirements

- C++17 compiler
- CMake 3.16 or newer
- RocheDB core shared library: `lib/librochedb.so`

Build RocheDB core first:

```sh
git clone https://github.com/puffball1567/rochedb.git
cd rochedb
nimble install -y
nim c --app:lib -d:release --nimcache:/tmp/nimcache_roche_capi -o:lib/librochedb.so src/rochedb_capi.nim
```

## Build The Smoke Test

From this repository:

```sh
cmake -S . -B build -DROCHEDB_CORE_DIR=/path/to/rochedb
cmake --build build
./build/rochedb_cpp_contract_smoke
```

Alternatively pass the library path directly:

```sh
cmake -S . -B build -DROCHEDB_LIBRARY=/path/to/librochedb.so
cmake --build build
LD_LIBRARY_PATH=/path/to ./build/rochedb_cpp_contract_smoke
```

## Minimal Example

```cpp
#include <iostream>
#include <string>
#include "rochedb/rochedb.hpp"

int main() {
  auto db = rochedb::Db::openDir("data", 8);
  db.setRingDescription("docs/japan", "Japanese documentation");

  auto id = db.put("docs/japan", R"({"title":"hello"})");
  auto payload = db.getString(id);

  if (payload) {
    std::cout << *payload << "\n";
  }
}
```

## Library Discovery

The C++ wrapper links to RocheDB's C ABI. For local builds, prefer one of:

- `-DROCHEDB_CORE_DIR=/path/to/rochedb`
- `-DROCHEDB_LIBRARY=/path/to/librochedb.so`
- environment variable `ROCHEDB_CORE_DIR`
- environment variable `ROCHEDB_LIBRARY`

The bundled `include/rochedb.h` is copied from RocheDB core and should match the
core library version you build against.
