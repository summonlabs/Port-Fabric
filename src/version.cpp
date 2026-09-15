#include "portfabric/version.hpp"

#include "text.hpp"

namespace portfabric {

std::string version_string() {
  std::string out;
  detail::append_u64(out, kVersionMajor);
  out.push_back('.');
  detail::append_u64(out, kVersionMinor);
  out.push_back('.');
  detail::append_u64(out, kVersionPatch);
  return out;
}

std::string_view product_name() noexcept { return "Port Fabric"; }

}  // namespace portfabric
