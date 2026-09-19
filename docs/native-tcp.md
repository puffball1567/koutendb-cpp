# Native TCP (Development)

The existing header-only C ABI wrapper remains unchanged. The optional
`KoutenDB::tcp` target is a compiled C++17 client with no libkoutendb dependency.

Build dependencies: Asio, OpenSSL, libsodium, nlohmann_json, CMake and a C++17
compiler. For example on Debian/Ubuntu, install `libasio-dev libssl-dev
libsodium-dev nlohmann-json3-dev pkg-config cmake g++`.

```sh
cmake -S . -B build-tcp \
  -DKOUTENDB_CPP_BUILD_EXAMPLES=OFF \
  -DKOUTENDB_CPP_BUILD_TCP=ON \
  -DKOUTENDB_CPP_TCP_TESTS=ON
cmake --build build-tcp
ctest --test-dir build-tcp --output-on-failure
```

If Asio/JSON are not installed, explicitly set
`-DKOUTENDB_CPP_FETCH_DEPS=ON` to download pinned official revisions. OpenSSL
and libsodium must still be installed. Downloads are never implicit by default.

Link applications to `KoutenDB::tcp` after `add_subdirectory`. The embedded
examples can remain disabled; they are the only targets requiring libkoutendb.

```cpp
#include <koutendb/tcp.hpp>

koutendb::tcp::Client db({"127.0.0.1:17301"});
auto id = db.put_json("articles", {{"title", "Hello"}});
auto value = db.get_json(id);
db.close();
```

Pass `Options` as the constructor's second argument for credentials, galaxy,
timeouts and TLS. Verified TLS uses `tls=true`, optional
`tls_ca_file="ca.pem"`, and `tls_server_name="db.example.com"`.
Credentials are `username`, `password`, `auth_token`, `secret_key`.

Errors use `koutendb::tcp::Error` with a typed `ErrorKind`.
Binary payloads use std::string with explicit lengths, so embedded NUL bytes
are preserved. Id::str()/Id::parse() preserve all six wire fields.

The synchronous API serializes client operations with a mutex. It uses Asio
asynchronous I/O internally for cancellation/deadlines, not an application-owned
event loop. OS name resolution may delay completion of resolver cancellation.
Close is terminal. This target initially supports Linux and macOS.

```sh
bash ../koutendb/scripts/native_driver_conformance.sh "$PWD/build-tcp/koutendb_tcp_adapter"
```

## Server Setup

Run a TLS-enabled `koutend` build. For a local-only first test:

```sh
koutend --id=0 --peers=127.0.0.1:17301 --data=./kouten-data
```

Keep plaintext connections on localhost or an isolated, trusted private network.
A Docker network is not a substitute for access control. Use verified TLS when
traffic crosses a trust boundary. For password authentication, start the server
with `--user=app --password=...`; prefer the server's configuration/secret
management facilities for production rather than putting secrets in shell history.

Native TCP implements wire version 1: WIREVER, CODECMETA, PUTR, GETID, QRYID,
HEALTH, authentication and bounded FWD handling. It is not a replacement for
every embedded/admin API. It uses server-provided IDs and does not calculate
ring placement or orbit ownership. Peer ordering must match the server cluster
configuration, because explicit redirect owners are node indexes.

## Safety Contract

- Every new connection authenticates, checks WIREVER and enables codec metadata
  before sending application requests. Unsupported versions fail closed.
- Headers are bounded to 8 KiB; payload frames default to at most 64 MiB.
  The configurable payload cap cannot exceed that hard limit.
- Partial reads/writes are handled. A read deadline covers the complete response,
  not a fresh timeout for every fragment.
- A read may reconnect and retry once. An unknown write outcome is never retried.
- After a broken or malformed response the connection is discarded.
- Redirects default to eight hops (configurable up to 32), and an out-of-range
  owner is rejected. Missing values do not trigger a scan of every server.
- CA and hostname verification are enabled by default. TLS 1.2 is the minimum.
  Insecure verification bypass is explicitly development-only.
- Password/token and shared-secret challenge authentication are supported.
  Library transport errors do not include raw server error text or credentials.

A successful send is not proof that a write committed. If the connection breaks
or the reply is malformed after a PUT may have been sent, handle an
**indeterminate write** separately from a definite server rejection. Do not
blindly repeat the insert or assume a fallback database is now authoritative.
Reconcile at the application level until a server-side idempotency contract is
available.

The pre-v1 protocol is version-checked, not promised compatible with future
versions. Authentication errors, protocol errors, connection failures, timeouts,
server rejections and indeterminate writes are distinguishable.

## Verification

The adapter in this repository runs against KoutenDB's language-independent
`scripts/native_driver_conformance.py` suite, pinned in CI to core commit
`e36b424bcfd9cd0dfa24ae121f4b4dd028b0eaac`.

The shared matrix covers 27 scripted cases: fragmented/empty/Unicode/binary
responses, missing values, projections, invalid lengths/codecs/headers, redacted
server errors, version mismatches, connection loss, partial-response retry,
timeouts, backpressure, redirects and poisoned-connection disposal.
Six real-server configurations cover plaintext, password, token, shared-secret,
TLS and TLS plus shared-secret; these include 1 MiB round trips, invalid
credentials, untrusted certificates and hostname mismatch.

These are bounded correctness/integration checks, not endurance or throughput
benchmarks. Linux results are checked locally; Linux/macOS CI must pass before
release. Existing embedded regressions remain separate from native TCP checks.
