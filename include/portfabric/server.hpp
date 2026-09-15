#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

#include "portfabric/engine.hpp"
#include "portfabric/export.hpp"
#include "portfabric/outcome.hpp"
#include "portfabric/protocol.hpp"

namespace portfabric {

/// Service boundary the coordinator server dispatches into.
///
/// The server owns framing, session lifetime and transport robustness. The
/// service owns semantics. A service implementation must be safe to call from
/// several session threads at once.
class PF_EXPORT CoordinatorService {
 public:
  CoordinatorService() = default;
  virtual ~CoordinatorService();

  CoordinatorService(const CoordinatorService&) = delete;
  CoordinatorService& operator=(const CoordinatorService&) = delete;

  /// Session opening. The implementation may correct the epoch the client sent
  /// so that the client learns the current coordinator epoch.
  virtual Outcome on_hello(const HelloMessage& message, CoordinatorEpoch& epoch) = 0;

  virtual Outcome on_register_publisher(const RegisterPublisherMessage& message) = 0;

  virtual MutationResult on_mutation(const PortMutationMessage& message) = 0;

  virtual QueryResponse on_query(const QueryMessage& message) = 0;

  virtual Outcome on_fence(const FenceMessage& message) = 0;
};

/// Coordinator service backed by a PortFabricEngine.
class PF_EXPORT EngineService : public CoordinatorService {
 public:
  explicit EngineService(PortFabricEngine& engine);

  Outcome on_hello(const HelloMessage& message, CoordinatorEpoch& epoch) override;
  Outcome on_register_publisher(const RegisterPublisherMessage& message) override;
  MutationResult on_mutation(const PortMutationMessage& message) override;
  QueryResponse on_query(const QueryMessage& message) override;
  Outcome on_fence(const FenceMessage& message) override;

  /// Translates a wire mutation message into the typed request and runs it.
  /// Exposed because the CLI and the proofs drive the same translation.
  static MutationResult dispatch(PortFabricEngine& engine, const PortMutationMessage& message);

 private:
  PortFabricEngine& engine_;
};

struct PF_EXPORT ServerConfig {
  std::string address = "127.0.0.1";
  /// Zero selects an ephemeral port, reported by bound_port().
  std::uint16_t port = 0;
  std::size_t max_sessions = 64;
};

/// Loopback control server.
///
/// One thread accepts, one thread per session. Shutdown signals the stop handle
/// and shuts every session socket down, so a session blocked in a receive wakes
/// deterministically and no thread is ever joined from itself.
class PF_EXPORT CoordinatorServer {
 public:
  CoordinatorServer(const ServerConfig& config, CoordinatorService& service);
  ~CoordinatorServer();

  CoordinatorServer(const CoordinatorServer&) = delete;
  CoordinatorServer& operator=(const CoordinatorServer&) = delete;

  /// Binds, listens and starts the accept thread.
  Outcome start();

  /// Requests shutdown without waiting.
  void stop();

  /// Requests shutdown and joins every thread.
  Outcome join();

  std::uint16_t bound_port() const;
  std::size_t active_sessions() const;
  std::size_t total_sessions() const;
  bool running() const;
  std::string last_error() const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace portfabric
