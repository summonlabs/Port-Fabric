# Port Fabric

Port Fabric is the authoritative port-governance runtime of the Distributed Fabric
Infrastructure ("Fabric OS") stack. It answers one question:

> What exact fabric port exists on this device generation, how is that port
> configured, which capabilities and administrative state apply to it, who owns or
> controls it, which configuration generation is authoritative, and when must a
> port operation be rejected, fenced, superseded, retired, or revalidated?

It is a vendor-neutral C++20 library with a coordinator runtime, a port agent, an
inspection tool and a CMake package. It has no third-party dependencies.

Version 1.0.0. Persisted state format version 1. Control protocol version 1.

---

## 1. Systems boundary

Port Fabric governs **the port as a configurable, generation-bound infrastructure
object**. It consumes canonical identity and structural context from its sibling
runtimes and never duplicates them.

### It owns

* canonical port reference consumption and the binding of a canonical `PortId` to
  a governed record;
* port lifecycle: unconfigured, configured, active, administratively disabled,
  draining, maintenance, revalidation required, superseded, retired, conflicted;
* port configuration, configuration generations and configuration authority;
* administrative state, including drain and maintenance intent;
* port control ownership and the process incarnation that holds it;
* port mode, local role, speed, lanes, MTU, FEC, autonegotiation, duplex, pause,
  breakout and port-local aggregation membership;
* capability binding against current capability evidence, and fail-closed handling
  of UNKNOWN capability;
* profile definitions and profile assignment, generation bound;
* deterministic mutation explanations, configuration diffs and content digests;
* stale-generation, stale-incarnation and stale-epoch rejection;
* immutable state snapshots with explicit currentness;
* versioned, integrity-checked persistence and conservative restart recovery;
* distributed configuration publication over a bounded framed protocol.

### It does not own

* canonical entity identity creation (Fabric Registry owns identity);
* topology graph construction and structural truth (Fabric Topology);
* dynamic link UP/DOWN/DEGRADED truth and link evidence currentness (Link State
  Fabric);
* broader canonical capability truth (Fabric Capability Registry);
* route lifecycle, path computation, path authority or traffic engineering (Route
  Fabric, Path Planner);
* bandwidth reservation, network-wide QoS policy, queue scheduling, shared packet
  buffer governance or congestion control (Queue Fabric, Buffer Fabric and the
  congestion control plane);
* flow scheduling and optical path planning.

Port Fabric governs the port. Everything else is somebody else's authority.

---

## 2. Separation of concepts

A port existing is not the same as the port being configured, enabled, applied,
operationally up, capable, or authorized. Port Fabric keeps these dimensions
separate and never derives one from another:

| Dimension | Owner | Where it lives |
| --- | --- | --- |
| Canonical identity | Fabric Registry | `PortId`, `DeviceId` consumed as references |
| Structural context | Fabric Topology | bound as a topology generation |
| Operational link condition | Link State Fabric | **not modelled here at all** |
| Administrative intent | Port Fabric | `AdministrativeState` |
| Desired configuration | Port Fabric | committed `PortConfiguration` |
| Applied configuration | device adapter | `AppliedEvidence` |
| Capability truth | Fabric Capability Registry | bound `CapabilityBinding` |
| Mutation authority | Port Fabric | `PortOwnership` + publisher incarnation |
| Process incarnation | Port Fabric | `WorkerBootId` + `CoordinatorEpoch` |

An administratively enabled port may be operationally down; a disabled port may
have an observable carrier; a committed configuration may never have reached
hardware; a persisted configuration may be authoritative while the process that
wrote it is fenced.

---

## 3. Lifecycle

```
UNCONFIGURED ──configure──▶ CONFIGURED ──activate──▶ ACTIVE
      │                          │  ▲                   │
      │                          │  └──enable───────────┼──▶ ADMIN_DISABLED
      │                          │                      ├──▶ DRAINING ──▶ MAINTENANCE
      │                          │                      └──▶ MAINTENANCE
      │                          │
      └────────── require revalidation ──▶ REVALIDATION_REQUIRED ──reconcile──▶ CONFIGURED
                 supersede ─────────────▶ SUPERSEDED ────────────reconcile──▶ CONFIGURED
                 conflict ──────────────▶ CONFLICTED ────────────reconcile──▶ CONFIGURED
                 retire (any non-retired) ─────────────────────────────────▶ RETIRED (terminal)
```

Every `(state, transition)` pair is defined: an illegal transition is rejected
with `INVALID_LIFECYCLE_TRANSITION` and a stable reason code, and a retired port
is never resurrected by any transition.

---

## 4. Configuration model

A committed configuration binds: the port identity, its parent device and device
generation, the topology generation, the configuration record identity and
generation, the administrative state, the port mode (physical or logical) and
protocol family, the speed intent (forced rate or automatic), duplex, MTU, lane
configuration, breakout mode, autonegotiation policy, FEC mode, pause mode,
port-local aggregation membership, profile binding, local role, logical derivation
for derived ports, the capability binding generation and the configuration
provenance.

Rules that are enforced structurally rather than by convention:

* speeds are integer bit rates with checked arithmetic; a display string is never
  the internal representation;
* the lane configuration must realise the configured total rate exactly;
* the lane count is bounded and must divide across the breakout fan-out;
* MTU is bounded and must be inside the capability range when evidence exists;
* a physical port may not carry logical derivation fields, and a logical port must
  carry them;
* a configuration is validated as a whole before it can be committed.

---

## 5. Desired versus applied configuration

Port Fabric commits **desired** configuration and records **applied evidence**
separately:

* `NOT_ATTEMPTED` — desired state only, no adapter was asked to act;
* `APPLIED` — an adapter reported a completed apply, without independent readback;
* `VERIFIED` — a readback confirmed the device matches, for that generation;
* `APPLY_FAILED` — the adapter reported a definite failure;
* `APPLY_OUTCOME_UNKNOWN` — the apply outcome is unknown and must be reconciled;
* `UNSUPPORTED` — no adapter exists for this port class.

An apply that fails is **not committed**: the mutation returns `APPLY_FAILED` and
authoritative state is unchanged. An apply whose outcome is unknown is also not
committed; the port is fenced into `REVALIDATION_REQUIRED` with
`APPLY_OUTCOME_UNKNOWN` evidence, because the device may hold a configuration this
runtime never committed. A successful apply that loses the compare-and-commit race
leaves the same conservative state.

---

## 6. Profiles

A profile is an immutable, generation-bound bundle of configuration dimensions.
`(identifier, generation)` is written once: redefining it with different content is
rejected, and a newer generation never rewrites an already committed configuration.
Ports whose bound profile generation is older than the latest defined generation are
reported by `ports_with_stale_profile_binding()` and only change when an explicit
profile assignment is performed.

---

## 7. Capability binding

Configuration is validated against bound capability evidence: supported speeds,
lane modes, breakout support and fan-out, FEC modes, autonegotiation modes, pause
modes, protocol families, MTU bounds, duplex, logical interface support, queue
bounds and transceiver class.

The rule is fail-closed. A dimension the evidence does not positively support is
rejected, and UNKNOWN evidence rejects every configuration that depends on it:
`CAPABILITY_UNKNOWN`, `CAPABILITY_UNAVAILABLE`, `UNSUPPORTED_CONFIGURATION` or
`TRANSCEIVER_UNKNOWN`, never a silent assumption. When capability evidence
advances and removes support for a committed configuration, that configuration is
**not** grandfathered: the port is fenced into `REVALIDATION_REQUIRED` with
`CAPABILITY_INVALIDATED` and the exact reason.

---

## 8. Ownership and authority

Ownership means **mutation authority**, not physical or commercial ownership. An
ownership record binds an owner kind (switch agent, host agent, network
controller, administrative control plane, delegated subsystem), the stable owner
identity, an ownership generation, and the **process incarnation** that holds it —
publisher, worker boot and coordinator epoch. A restart produces a new
`WorkerBootId`, so a restarted process never silently retains authority merely
because its stable identity is unchanged.

Every mutation carries an authority context: coordinator epoch, publisher, worker
boot, expected ownership identity and generation, expected configuration,
capability, topology and device generations, and a mutation attempt identity.
Authority is checked before semantics, so a fenced incarnation can never probe or
alter configuration.

Ownership transfer can name the receiving process incarnation explicitly, which is
how a port is handed from one live controller to another. Fencing an incarnation
releases the ownership it held, and a coordinator epoch advance releases ownership
granted under an earlier epoch.

---

## 9. Generations and idempotency

| Generation | Advances when |
| --- | --- |
| configuration | the committed configuration changes semantically |
| administrative | the administrative state changes |
| ownership | ownership is claimed, transferred or released |
| capability binding | capability evidence is rebound |
| topology | structural context advances |
| device | a device generation is recorded or advances |
| lifecycle | the lifecycle state changes |
| evidence | applied evidence is recorded |
| profile | a new profile generation is defined |
| coordinator epoch | a new coordinator process life starts |

Exact replay of a committed mutation — same attempt identity, same semantic
payload — returns `IDEMPOTENT` and advances nothing. The same attempt identity
with a different payload is `CONFLICT_DETECTED`. An expectation older than the
current generation is `STALE_*_GENERATION`. Mutations commit with
compare-and-commit over the configuration, lifecycle, administrative, ownership and
evidence generations, so a mutation prepared against superseded state is rejected
rather than applied.

---

## 10. Persistence and recovery

Durable state is written in a versioned container: a fixed header with magic,
format version, payload length, a CRC-32 over the header and a CRC-32 over the
payload, followed by a canonical, length-prefixed, bounded encoding. Writes go to a
temporary file in the same directory, are flushed, re-read and verified, and only
then atomically replace the previous container.

Recovery is deliberately conservative:

* durable desired configuration survives;
* every recovered configuration is placed in `REVALIDATION_REQUIRED`;
* verified application evidence is downgraded to `APPLY_OUTCOME_UNKNOWN`;
* ownership held by a previous process incarnation is released;
* every publisher incarnation from the previous process life is fenced;
* the coordinator epoch advances, so old-epoch traffic is rejected;
* mutation attempt identities are cleared, so an old replay is never classified as
  idempotent.

---

## 11. Hardware adapters

Device-specific behaviour lives behind `PortAdapter`: `apply()` performs a real
change, `readback()` reports what the device actually holds. Adapter calls always
run with **no engine lock held**, so a slow adapter cannot stall unrelated ports.
Capability evidence comes from a `CapabilityProvider` with the same property.

Two backends ship in the library:

* **`HostPlatform`** — read-only evidence about the local host's ports through the
  Windows interface table: interface identity (GUID), alias, description, PnP
  identity, administrative status, operational status, MTU, link speed and media
  type. It never changes host configuration.
* **`SyntheticFabric`, `SyntheticCapabilityProvider`, `SyntheticAdapter`** — a
  deterministic synthetic fabric with device inventories, capability class models
  from 1G to 800G including optical and InfiniBand classes, breakout children,
  external state, and injectable faults (permission denied, device disappeared,
  device replaced, unsupported parameter, partial apply, failed verification, stale
  handle, concurrent external change, adapter crash, transport failure).

---

## 12. REAL, SYNTHETIC and UNSUPPORTED

Nothing in this repository relabels synthetic evidence as physical proof.

**REAL** — validated on this host: local NIC enumeration (interface GUIDs, PnP
identities, administrative and operational status, MTU, link speed, media type),
including the onboard Realtek 5GbE adapter; the whole file-system and persistence
path; the loopback TCP transport; real multi-process coordinator restart, real
uncatchable worker termination, real fencing, real reincarnation and real
IPC-driven recovery proofs.

**SYNTHETIC** — switch-class port governance: 1G/10G/25G/40G/50G/100G/200G/400G/800G
port classes, optical and InfiniBand classes, breakout children, transceiver
requirements, high-speed capability change, multi-device inventories, and every
adapter failure mode. The synthetic fabric marks its evidence as synthetic.

**UNSUPPORTED** — physical switch port mutation, optical switch configuration,
InfiniBand fabric control, SmartNIC/DPU port control, breakout hardware, 400/800G
Ethernet hardware, and host NIC mutation (Port Fabric never reconfigures the host it
runs on). The distributed transport is implemented for Windows in this release; on
other platforms the runtime reports it as UNSUPPORTED rather than pretending it
works.

---

## 13. Build

Requirements: a C++20 compiler and CMake 3.25 or newer. No third-party libraries.

```
cmake -S . -B build/release -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/release
ctest --test-dir build/release --output-on-failure
```

Options: `PF_BUILD_TESTS`, `PF_BUILD_EXAMPLES`, `PF_BUILD_TOOLS`,
`PF_BUILD_BENCHMARKS`, `PF_ENABLE_ASAN`, `PF_WARNINGS_AS_ERRORS`. First-party
code is built with `/W4 /WX /permissive-` on MSVC and
`-Wall -Wextra -Wpedantic -Wshadow -Werror` elsewhere; no warning is suppressed.

`CMakePresets.json` provides `release`, `debug` and `asan` presets.

---

## 14. Test

The suite is split into focused executables and contains no timeouts: a hanging
test is a defect to diagnose, not something to mask.

| Suite | What it proves |
| --- | --- |
| `pf_test_model` | identifiers, generations, characteristics, lifecycle relation, outcomes, digests |
| `pf_test_engine` | configuration, lifecycle, administrative state, ownership, retirement, supersession, drift, indexes, snapshots |
| `pf_test_capability_profile` | capability validation matrix, fail-closed UNKNOWN, profile generations, breakout geometry |
| `pf_test_persistence` | round trip, conservative recovery, corruption, truncation, absurd counts, path validation, atomic replacement |
| `pf_test_concurrency` | parallel mutation, deterministic conflict resolution, adapter callbacks outside the state lock, shutdown with work in flight |
| `pf_test_adversarial` | malformed requests, stale authority, attempt reuse, text bounds, injected adapter failures, ambiguous completion, mutation storms |
| `pf_test_property` | seeded properties: generation monotonicity, stale rejection, retirement terminality, capability-consistent commits, index consistency, deterministic serialization |
| `pf_test_scale` | 10,000 ports, incremental mutation, bulk profile assignment, ownership invalidation, snapshots, 5,000-port persistence round trip |
| `pf_test_platform` | real host evidence, bounded file helpers, synthetic capability classes, adapter failure modes, truthful classification |
| `pf_test_distributed` | framing, malformed frames, integrity checks, message codecs, sessions, session bound, repeatable start/stop, shutdown with a blocked session |
| `pf_test_multiprocess` | real coordinator and agent processes: uncatchable worker death, fencing, old-boot replay rejection, fresh incarnation, durable configuration reconciliation, real coordinator kill and restart with epoch advance |

---

## 15. Install and use

```
cmake --install build/release --prefix <prefix>
```

Downstream projects use the installed package:

```cmake
find_package(PortFabric CONFIG REQUIRED)
target_link_libraries(app PRIVATE SummonSoftwareLabs::PortFabric)
```

`validation/consumer` is an independent consumer that builds from the installed
package only, binds a port, commits a synthetic configuration, queries it and
prints a snapshot.

---

## 16. Examples

`examples/` contains ten focused programs, each built against the public API only:

| Example | Demonstrates |
| --- | --- |
| `ex01_basic_configuration` | binding a canonical port, claiming ownership, committing a configuration |
| `ex02_administrative_state` | disable, enable, drain, maintenance and resume |
| `ex03_profile_assignment` | generation-bound profiles and explicit re-evaluation |
| `ex04_stale_generation` | idempotent replay versus stale expectation |
| `ex05_capability_rejection` | unsupported configuration and capability-generation invalidation |
| `ex06_worker_fencing` | process-incarnation fencing and fresh-incarnation reclaim |
| `ex07_topology_invalidation` | topology generation advance and reconciliation |
| `ex08_device_replacement` | device replacement fencing and rebinding |
| `ex09_persistence_recovery` | persistence and conservative recovery |
| `ex10_synthetic_breakout` | synthetic breakout children with derivation provenance |

---

## 17. Command line tools

* `pf-coordinator` — hosts the authoritative runtime and the control server, loads
  and writes durable state, advances the coordinator epoch on start and fences every
  incarnation from the previous process life.
* `pf-worker` — a real port agent process: connects, registers its incarnation,
  claims ownership, commits configuration, and can be killed outright; its
  `replay` mode attempts a mutation with a caller-supplied incarnation so that a
  fenced boot is provably rejected.
* `pf-inspect` — deterministic inspection: `list-ports`, `show-port`,
  `show-config`, `show-lifecycle`, `show-admin`, `show-owner`,
  `show-profile`, `show-capability`, `show-applied`, `show-devices`,
  `show-profiles`, `explain`, `diff --set key=value`, `validate`,
  `snapshot`, `inspect-persistence` and `host-ports`.

---

## 18. Benchmarks

Build with `-DPF_BUILD_BENCHMARKS=ON` and run `pf_benchmarks`. It measures only
completed work — a configuration is counted after the runtime returned an
authoritative outcome — at 1,000, 10,000 and 100,000 ports, plus lookups, indexed
queries, snapshot construction, concurrent reads and a persistence round trip.

---

## 19. Threading model

All public runtime methods are safe to call concurrently. Queries return values;
internal storage is never exposed. Adapter and capability-provider callbacks run
with no engine lock held, so they may block without stalling unrelated ports. Lock
order is profiles, then authority, then records: a session never joins a thread
while holding the lock that thread needs, and a client never re-enters its own
mutex.

---

## 20. Limitations

* The distributed transport is implemented for Windows in this release; on other
  platforms it reports `UNSUPPORTED`.
* Port Fabric performs no routing, path computation, traffic engineering, queue
  scheduling, buffer governance or congestion control, and models no operational
  link condition: those belong to other Fabric OS runtimes.
* Switch-class behaviour is exercised through the synthetic fabric. No physical
  switch, optical, InfiniBand, SmartNIC/DPU or breakout hardware is present on the
  validation host, and no such claim is made.
* Host NIC evidence is read-only by design: Port Fabric never reconfigures the host
  it runs on.
* Capacity bounds are explicit and finite: at most 1,000,000 governed ports, 4,096
  ports per device, 65,536 profiles and publishers, 1 MiB control frames and 512 MiB
  persistence containers. Exceeding a bound is rejected before memory is reserved.
* Profiles, capability evidence and device generations are supplied by the caller or
  by an adapter. Port Fabric consumes that truth; it does not create it.

## License

Apache License 2.0. Copyright 2026 Summon Software Labs. No telemetry transmission.
