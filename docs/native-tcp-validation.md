# Native TCP Validation

Date: 2026-09-19

Local Linux verification against the shared KoutenDB conformance harness at
core commit `e36b424bcfd9cd0dfa24ae121f4b4dd028b0eaac`:

- All 27 scripted protocol/failure cases passed.
- All six real-server modes passed: plain, password, token, secret, TLS, TLS+secret.
- Verified Unicode, empty/binary data and 1 MiB payload round trips.
- Verified invalid credentials, certificate rejection, hostname mismatch,
  bounded redirects, partial frames, timeout, disconnection and unsafe-write replay prevention.

- CTest ID/configuration checks: passed.
- Existing C ABI contract smoke: passed.
- GCC 11 build with -Wall: passed.
- Clang 14 ASan + UBSan + leak detection: shared conformance matrix passed.
- ELF dependency inspection: no libkoutendb dependency.

The local Clang 14 sanitizer runtime intermittently crashed at startup even for
an empty main (5 failures in 30 launches). The sanitizer-only executable was
rebuilt with -fno-pie/-no-pie; the complete matrix then passed. This does not
change production compiler flags, disable sanitizers or suppress findings.

The GitHub workflow runs the shared conformance matrix on Linux and macOS.
See the release commit's workflow checks for CI results. The local results above
are correctness/integration checks, not load or long-duration operational tests.

See [native TCP usage and reproduction commands](native-tcp.md).
