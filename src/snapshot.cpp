#include "portfabric/snapshot.hpp"

#include "text.hpp"

namespace portfabric {

const PortRecord* Snapshot::find(const PortId& port) const noexcept {
  for (const PortRecord& record : records) {
    if (record.port == port) {
      return &record;
    }
  }
  return nullptr;
}

std::string Snapshot::to_string() const {
  std::string out("snapshot=");
  out.append(id.valid() ? id.to_string() : std::string("none"));
  out.append(" generation=");
  out.append(generation.valid() ? generation.to_string() : std::string("none"));
  out.append(" epoch=");
  out.append(epoch.valid() ? epoch.to_string() : std::string("none"));
  out.append(" topology=");
  out.append(topology.valid() ? topology.to_string() : std::string("none"));
  out.append(" ports=");
  detail::append_u64(out, static_cast<std::uint64_t>(records.size()));
  out.append(" digest=");
  out.append(digest.to_hex());
  return out;
}

}  // namespace portfabric
