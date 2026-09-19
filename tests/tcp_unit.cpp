#include <koutendb/tcp.hpp>
#include <iostream>

using namespace koutendb::tcp;
int main() {
  unsigned failures = 0;
  try { Id{}.str(); ++failures; } catch (const Error&) {}
  for (const auto* text : {"-1:0:1:1:60:0", "1:0:1:nan:60:0", "1:0:1:1:0:0", "1:4294967296:1:1:60:0"}) {
    try { Id::parse(text); ++failures; } catch (const Error&) {}
  }
  const auto id = Id::parse("18446744073709551615:4294967295:4294967295:1:60:0");
  if (Id::parse(id.str()).parent != UINT64_MAX) ++failures;
  Options options; options.timeout = 0;
  try { Client db({"localhost:17301"}, options); ++failures; } catch (const std::invalid_argument&) {}
  if (failures) std::cerr << failures << " failed checks\n";
  return failures ? 1 : 0;
}
