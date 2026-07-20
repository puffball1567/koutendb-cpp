# KoutenDB C++ Driver

C++17 RAII wrapper for KoutenDB through the stable C ABI.

This repository is the generic OSS C++ driver. Unreal-specific module packaging,
Blueprint bindings, editor tooling, and engine lifecycle integration should live
in a separate Unreal plugin.

## Status

- package version: `0.1.2`
- license: Apache-2.0
- mode: header-only C++ wrapper over `libkoutendb.so`
- core ABI: KoutenDB C ABI version `2`

Implemented:

- embedded `open` / `openDir`
- authenticated cluster `connectAuth`
- TLS cluster `connectAuthTls`
- `put` / `putVec`
- codec-aware `putJson` / `putNif` / `putBif`
- codec-aware vector writes: `putJsonVec` / `putNifVec` / `putBifVec`
- `get` / `getEncoded` / `batchGet`
- ring reads through `readRingJson`
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

For simulation games, KoutenDB can act as a local context store for world, NPC, faction, event, and memory data that accumulates over time and should be retrieved selectively. Applications can still decide how much transient state belongs in KoutenDB; the driver does not force a state-management pattern.

## Requirements

- C++17 compiler
- CMake 3.16 or newer
- KoutenDB core shared library: `lib/libkoutendb.so`

Build KoutenDB core first:

```sh
git clone https://github.com/puffball1567/koutendb.git
cd koutendb
nimble install -y
nim c --app:lib -d:release --nimcache:/tmp/nimcache_kouten_capi -o:lib/libkoutendb.so src/koutendb_capi.nim
```

## Build The Smoke Test

From this repository:

```sh
cmake -S . -B build -DKOUTENDB_CORE_DIR=/path/to/koutendb
cmake --build build
./build/koutendb_cpp_contract_smoke
```

Alternatively pass the library path directly:

```sh
cmake -S . -B build -DKOUTENDB_LIBRARY=/path/to/libkoutendb.so
cmake --build build
LD_LIBRARY_PATH=/path/to ./build/koutendb_cpp_contract_smoke
```

## Minimal Example

```cpp
#include <iostream>
#include <string>
#include "koutendb/koutendb.hpp"

int main() {
  auto db = koutendb::Db::openDir("data", 8);
  db.setRingDescription("docs/japan", "Japanese documentation");

  auto id = db.putJson("docs/japan", R"({"title":"hello"})");
  auto payload = db.getEncoded(id);

  if (payload) {
    std::cout << std::string(payload->payload.begin(), payload->payload.end())
              << "\n";
  }

  std::cout << db.readRingJson("docs/japan", "{}", "{ title }", 10) << "\n";
}
```

## TLS

TLS requires an KoutenDB core built with `-d:ssl`. A library built without it
fails a TLS connect with `TLS support requires building KoutenDB with -d:ssl`.

To reach a server whose certificate is signed by a private CA — or is
self-signed — point at the certificate PEM. Verification stays on:

```cpp
auto db = koutendb::Db::connectAuthTls(
    "127.0.0.1:17651", "alice", "secret", "", "shared-secret", "",
    "/path/to/server.crt");
```

The final `tlsInsecureSkipVerify` argument disables certificate verification.
The connection is then encrypted but unauthenticated and trivially
impersonable, so it is for local smoke tests only — never a production server.
Prefer a `tlsCaFile` for self-signed certificates. See
`examples/cluster_tls.cpp`.

## Library Discovery

The C++ wrapper links to KoutenDB's C ABI. For local builds, prefer one of:

- `-DKOUTENDB_CORE_DIR=/path/to/koutendb`
- `-DKOUTENDB_LIBRARY=/path/to/libkoutendb.so`
- environment variable `KOUTENDB_CORE_DIR`
- environment variable `KOUTENDB_LIBRARY`

The bundled `include/koutendb.h` is copied from KoutenDB core and should match the
core library version you build against.
