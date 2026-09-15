#include "portfabric/platform.hpp"

#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#include "text.hpp"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2ipdef.h>
#include <windows.h>
#include <iphlpapi.h>
#include <netioapi.h>
#else
#include <thread>
#endif

namespace portfabric {
namespace {

#ifdef _WIN32

std::wstring to_wide(std::string_view text) {
  if (text.empty()) {
    return std::wstring();
  }
  const int size = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                                       nullptr, 0);
  if (size <= 0) {
    return std::wstring();
  }
  std::wstring out(static_cast<std::size_t>(size), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), size);
  return out;
}

std::string from_wide(const wchar_t* text) {
  if (text == nullptr || text[0] == L'\0') {
    return std::string();
  }
  const int size = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
  if (size <= 1) {
    return std::string();
  }
  std::string out(static_cast<std::size_t>(size - 1), '\0');
  WideCharToMultiByte(CP_UTF8, 0, text, -1, out.data(), size, nullptr, nullptr);
  return out;
}

std::string windows_error_text(const char* prefix) {
  const DWORD code = GetLastError();
  std::string text = prefix;
  text.append(" (windows error ");
  text.append(std::to_string(static_cast<unsigned long>(code)));
  text.push_back(')');
  return text;
}

std::string media_type_label(NDIS_MEDIUM medium) {
  switch (medium) {
    case NdisMedium802_3:
      return "802.3";
    case NdisMedium802_5:
      return "802.5";
    case NdisMediumFddi:
      return "fddi";
    case NdisMediumWan:
      return "wan";
    case NdisMediumLocalTalk:
      return "localtalk";
    case NdisMediumDix:
      return "dix";
    case NdisMediumArcnetRaw:
      return "arcnet-raw";
    case NdisMediumArcnet878_2:
      return "arcnet-878.2";
    case NdisMediumAtm:
      return "atm";
    case NdisMediumWirelessWan:
      return "wireless-wan";
    case NdisMediumIrda:
      return "irda";
    case NdisMediumBpc:
      return "bpc";
    case NdisMediumCoWan:
      return "co-wan";
    case NdisMedium1394:
      return "1394";
    case NdisMediumInfiniBand:
      return "infiniband";
    case NdisMediumTunnel:
      return "tunnel";
    case NdisMediumNative802_11:
      return "802.11";
    case NdisMediumLoopback:
      return "loopback";
    case NdisMediumWiMAX:
      return "wimax";
    case NdisMediumIP:
      return "ip";
    default:
      return "unknown";
  }
}

std::string pnp_identity_for(const GUID& interface_guid) {
  // The PnP instance identity of a network interface is published by the
  // operating system under the network class key, keyed by the interface GUID.
  const wchar_t* kNetworkClassKey =
      L"SYSTEM\\CurrentControlSet\\Control\\Network\\{4d36e972-e325-11ce-bfc1-08002be10318}";
  wchar_t guid_text[64] = {};
  if (StringFromGUID2(interface_guid, guid_text, 64) <= 0) {
    return std::string();
  }
  HKEY class_key = nullptr;
  if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, kNetworkClassKey, 0, KEY_READ, &class_key) !=
      ERROR_SUCCESS) {
    return std::string();
  }
  HKEY connection_key = nullptr;
  const LSTATUS open_status =
      RegOpenKeyExW(class_key, guid_text, 0, KEY_READ, &connection_key);
  RegCloseKey(class_key);
  if (open_status != ERROR_SUCCESS) {
    return std::string();
  }
  HKEY connection_sub_key = nullptr;
  const LSTATUS sub_status =
      RegOpenKeyExW(connection_key, L"Connection", 0, KEY_READ, &connection_sub_key);
  RegCloseKey(connection_key);
  if (sub_status != ERROR_SUCCESS) {
    return std::string();
  }
  wchar_t value[512] = {};
  DWORD value_size = sizeof(value);
  DWORD value_type = 0;
  const LSTATUS query_status = RegQueryValueExW(connection_sub_key, L"PnpInstanceID", nullptr,
                                                &value_type, reinterpret_cast<LPBYTE>(value),
                                                &value_size);
  RegCloseKey(connection_sub_key);
  if (query_status != ERROR_SUCCESS || value_type != REG_SZ) {
    return std::string();
  }
  return from_wide(value);
}

#endif  // _WIN32

}  // namespace

std::string HostPortEvidence::to_string() const {
  std::string out("identity=");
  out.append(interface_identity.empty() ? "unknown" : interface_identity);
  out.append(" class=");
  out.append(portfabric::to_string(entity_class));
  out.append(" alias=");
  out.append(alias);
  out.append(" admin=");
  detail::append_bool(out, admin_enabled);
  out.append(" operational=");
  detail::append_bool(out, operational_up);
  out.append(" mtu=");
  detail::append_u64(out, mtu);
  out.append(" speed=");
  out.append(speed_bits_per_second == 0 ? std::string("unknown")
                                        : detail::format_u64(speed_bits_per_second));
  out.append(" media=");
  out.append(media_type);
  if (!pnp_identity.empty()) {
    out.append(" pnp=");
    out.append(pnp_identity);
  }
  return out;
}

bool HostPlatform::available() noexcept {
#ifdef _WIN32
  return true;
#else
  return false;
#endif
}

std::string_view HostPlatform::backend_label() noexcept {
#ifdef _WIN32
  return "windows-mib-if";
#else
  return "unsupported";
#endif
}

std::vector<HostPortEvidence> HostPlatform::enumerate_ports(std::string& error) {
  std::vector<HostPortEvidence> ports;
  error.clear();
#ifdef _WIN32
  PMIB_IF_TABLE2 table = nullptr;
  const NETIO_STATUS status = GetIfTable2(&table);
  if (status != NO_ERROR || table == nullptr) {
    error = "the host interface table could not be read (netio status ";
    error.append(std::to_string(static_cast<unsigned long>(status)));
    error.push_back(')');
    return ports;
  }
  const std::size_t count = table->NumEntries;
  if (count > 4096) {
    FreeMibTable(table);
    error = "the host reported an implausible interface count";
    return ports;
  }
  ports.reserve(count);
  for (std::size_t index = 0; index < count; ++index) {
    const MIB_IF_ROW2& row = table->Table[index];
    HostPortEvidence evidence;
    {
      wchar_t guid_text[64] = {};
      if (StringFromGUID2(row.InterfaceGuid, guid_text, 64) > 0) {
        evidence.interface_identity = from_wide(guid_text);
      }
    }
    evidence.alias = from_wide(row.Alias);
    evidence.description = from_wide(row.Description);
    evidence.pnp_identity = pnp_identity_for(row.InterfaceGuid);
    evidence.entity_class = PortEntityClass::NicPort;
    evidence.admin_enabled =
        static_cast<int>(row.AdminStatus) == static_cast<int>(NET_IF_ADMIN_STATUS_UP);
    evidence.operational_up =
        static_cast<int>(row.OperStatus) == static_cast<int>(NET_IF_OPER_STATUS_UP);
    evidence.speed_bits_per_second = row.TransmitLinkSpeed;
    evidence.mtu = row.Mtu;
    evidence.media_type = media_type_label(row.MediaType);
    ports.push_back(std::move(evidence));
  }
  FreeMibTable(table);
#else
  error = "host port enumeration is UNSUPPORTED on this platform";
#endif
  return ports;
}

std::uint32_t HostPlatform::processor_count() noexcept {
#ifdef _WIN32
  const DWORD count = GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);
  return count == 0 ? 1u : static_cast<std::uint32_t>(count);
#else
  const unsigned int count = std::thread::hardware_concurrency();
  return count == 0 ? 1u : static_cast<std::uint32_t>(count);
#endif
}

std::uint32_t HostPlatform::current_process_id() noexcept {
#ifdef _WIN32
  return static_cast<std::uint32_t>(GetCurrentProcessId());
#else
  return 0;
#endif
}

Outcome validate_file_path(const std::string& path) {
  if (path.empty()) {
    return Outcome::failure(OutcomeCode::MalformedRequest, "the file path is empty");
  }
  if (path.size() > 4096) {
    return Outcome::failure(OutcomeCode::ResourceBoundExceeded, "the file path is too long");
  }
  for (const char ch : path) {
    const auto value = static_cast<unsigned char>(ch);
    if (value < 0x20u || value == 0x7fu) {
      return Outcome::failure(OutcomeCode::MalformedRequest,
                              "the file path contains a control character");
    }
  }
  std::error_code error_code;
  const std::filesystem::path candidate(path);
  for (const auto& part : candidate) {
    if (part == "..") {
      return Outcome::failure(OutcomeCode::MalformedRequest,
                              "the file path contains a traversal component");
    }
  }
  std::filesystem::path parent = candidate.parent_path();
  if (parent.empty()) {
    parent = std::filesystem::current_path(error_code);
  }
  if (!std::filesystem::exists(parent, error_code) ||
      !std::filesystem::is_directory(parent, error_code)) {
    return Outcome::failure(OutcomeCode::PersistenceIoFailure,
                            "the parent directory of the path does not exist");
  }
  return Outcome::success("path accepted");
}

bool file_exists(const std::string& path) {
  std::error_code error_code;
  return std::filesystem::is_regular_file(std::filesystem::path(path), error_code);
}

bool directory_exists(const std::string& path) {
  std::error_code error_code;
  return std::filesystem::is_directory(std::filesystem::path(path), error_code);
}

Outcome read_file_bounded(const std::string& path, std::uint64_t max_bytes, std::string& bytes) {
  bytes.clear();
  if (const Outcome path_check = validate_file_path(path); !path_check.ok()) {
    return path_check;
  }
  std::error_code error_code;
  const std::uintmax_t size = std::filesystem::file_size(std::filesystem::path(path), error_code);
  if (error_code) {
    return Outcome::failure(OutcomeCode::PersistenceIoFailure, "the file size could not be read");
  }
  if (size > max_bytes) {
    return Outcome::failure(OutcomeCode::PersistenceBoundsExceeded,
                            "the file exceeds the configured byte bound");
  }
#ifdef _WIN32
  HANDLE handle = CreateFileW(to_wide(path).c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (handle == INVALID_HANDLE_VALUE) {
    return Outcome::failure(OutcomeCode::PersistenceIoFailure,
                            windows_error_text("the file could not be opened for reading"));
  }
  bytes.resize(static_cast<std::size_t>(size));
  std::size_t offset = 0;
  bool ok = true;
  while (offset < bytes.size()) {
    const DWORD chunk = static_cast<DWORD>(
        (std::min)(static_cast<std::size_t>(1u << 20), bytes.size() - offset));
    DWORD read = 0;
    if (!ReadFile(handle, bytes.data() + offset, chunk, &read, nullptr) || read == 0) {
      ok = false;
      break;
    }
    offset += read;
  }
  CloseHandle(handle);
  if (!ok) {
    bytes.clear();
    return Outcome::failure(OutcomeCode::PersistenceIoFailure, "the file could not be read");
  }
  return Outcome::success("file read");
#else
  std::FILE* file = std::fopen(path.c_str(), "rb");
  if (file == nullptr) {
    return Outcome::failure(OutcomeCode::PersistenceIoFailure, "the file could not be opened");
  }
  bytes.resize(static_cast<std::size_t>(size));
  const std::size_t read = std::fread(bytes.data(), 1, bytes.size(), file);
  std::fclose(file);
  if (read != bytes.size()) {
    bytes.clear();
    return Outcome::failure(OutcomeCode::PersistenceIoFailure, "the file could not be read");
  }
  return Outcome::success("file read");
#endif
}

Outcome write_file_flushed(const std::string& path, std::string_view bytes) {
  if (const Outcome path_check = validate_file_path(path); !path_check.ok()) {
    return path_check;
  }
#ifdef _WIN32
  HANDLE handle = CreateFileW(to_wide(path).c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
  if (handle == INVALID_HANDLE_VALUE) {
    return Outcome::failure(OutcomeCode::PersistenceIoFailure,
                            windows_error_text("the file could not be created"));
  }
  std::size_t offset = 0;
  bool ok = true;
  while (offset < bytes.size()) {
    const DWORD chunk = static_cast<DWORD>(
        (std::min)(static_cast<std::size_t>(1u << 20), bytes.size() - offset));
    DWORD written = 0;
    if (!WriteFile(handle, bytes.data() + offset, chunk, &written, nullptr) ||
        written != chunk) {
      ok = false;
      break;
    }
    offset += written;
  }
  if (ok && !FlushFileBuffers(handle)) {
    ok = false;
  }
  CloseHandle(handle);
  if (!ok) {
    DeleteFileW(to_wide(path).c_str());
    return Outcome::failure(OutcomeCode::PersistenceIoFailure, "the file could not be written");
  }
  return Outcome::success("file written and flushed");
#else
  std::FILE* file = std::fopen(path.c_str(), "wb");
  if (file == nullptr) {
    return Outcome::failure(OutcomeCode::PersistenceIoFailure, "the file could not be created");
  }
  const std::size_t written = std::fwrite(bytes.data(), 1, bytes.size(), file);
  const bool flushed = written == bytes.size() && std::fflush(file) == 0;
  std::fclose(file);
  if (!flushed) {
    std::remove(path.c_str());
    return Outcome::failure(OutcomeCode::PersistenceIoFailure, "the file could not be written");
  }
  return Outcome::success("file written and flushed");
#endif
}

Outcome atomic_replace_file(const std::string& source, const std::string& target) {
  if (const Outcome check = validate_file_path(source); !check.ok()) {
    return check;
  }
  if (const Outcome check = validate_file_path(target); !check.ok()) {
    return check;
  }
#ifdef _WIN32
  if (!MoveFileExW(to_wide(source).c_str(), to_wide(target).c_str(),
                   MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
    return Outcome::failure(OutcomeCode::PersistenceIoFailure,
                            windows_error_text("the container could not be replaced atomically"));
  }
  return Outcome::success("container replaced");
#else
  std::error_code error_code;
  std::filesystem::rename(std::filesystem::path(source), std::filesystem::path(target), error_code);
  if (error_code) {
    return Outcome::failure(OutcomeCode::PersistenceIoFailure,
                            "the container could not be replaced atomically");
  }
  return Outcome::success("container replaced");
#endif
}

Outcome remove_file(const std::string& path) {
  std::error_code error_code;
  std::filesystem::remove(std::filesystem::path(path), error_code);
  if (error_code) {
    return Outcome::failure(OutcomeCode::PersistenceIoFailure, "the file could not be removed");
  }
  return Outcome::success("file removed");
}

}  // namespace portfabric
