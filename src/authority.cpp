#include "portfabric/authority.hpp"

namespace portfabric {

std::string AuthorityContext::to_string() const {
  std::string out("epoch=");
  out.append(epoch.valid() ? epoch.to_string() : std::string("none"));
  out.append(" publisher=");
  out.append(publisher.valid() ? publisher.to_string() : std::string("none"));
  out.append(" boot=");
  out.append(boot.valid() ? boot.to_string() : std::string("none"));
  out.append(" ownership=");
  out.append(ownership.valid() ? ownership.to_string() : std::string("none"));
  out.append(" ownership_generation=");
  out.append(ownership_generation.valid() ? ownership_generation.to_string() : std::string("none"));
  out.append(" expected_configuration=");
  out.append(expected_configuration.valid() ? expected_configuration.to_string()
                                            : std::string("none"));
  out.append(" expected_capability=");
  out.append(expected_capability.valid() ? expected_capability.to_string() : std::string("none"));
  out.append(" expected_topology=");
  out.append(expected_topology.valid() ? expected_topology.to_string() : std::string("none"));
  out.append(" expected_device=");
  out.append(expected_device.valid() ? expected_device.to_string() : std::string("none"));
  out.append(" attempt=");
  out.append(attempt.valid() ? attempt.to_string() : std::string("none"));
  return out;
}

}  // namespace portfabric
