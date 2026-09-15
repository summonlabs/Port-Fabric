#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "portfabric/engine.hpp"
#include "portfabric/export.hpp"
#include "portfabric/outcome.hpp"
#include "portfabric/protocol.hpp"

namespace portfabric {

struct PF_EXPORT ClientConfig {
  std::string address = "127.0.0.1";
  std::uint16_t port = 0;
  /// Stable identity of the connecting process.
  PublisherId publisher;
  /// Incarnation identity of the connecting process. A new process must use a
  /// new WorkerBootId, otherwise the coordinator treats it as the same boot.
  WorkerBootId boot;
  /// Bounded role label.
  std::string role = "client";
};

/// Synchronous control-protocol client.
///
/// Calls are serialized internally, so one client instance may be used from
/// several threads. A client never retries a mutation automatically: a mutation
/// with an unknown outcome must be resolved by the caller through readback or
/// reconciliation.
class PF_EXPORT FabricClient {
 public:
  explicit FabricClient(const ClientConfig& config);
  ~FabricClient();

  FabricClient(const FabricClient&) = delete;
  FabricClient& operator=(const FabricClient&) = delete;

  /// Connects and performs the opening handshake. On success the coordinator
  /// epoch is reported through the return value of epoch().
  Outcome connect();

  Outcome register_publisher();

  MutationResult mutate(const PortMutationMessage& message);

  QueryResponse query(const QueryMessage& message);

  Outcome fence_publisher(const PublisherId& publisher, const WorkerBootId& boot,
                          std::string detail);

  void close();

  bool connected() const;
  CoordinatorEpoch epoch() const;
  const std::string& last_error() const;

 private:
  /// Closes the session. The caller must already hold the client mutex; the
  /// public close() takes it. Keeping the two distinct is what prevents a
  /// re-entrant lock on the failure paths of connect().
  void close_locked();

  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace portfabric
