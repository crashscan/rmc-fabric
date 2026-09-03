# Network Observation

## Phase 3 layout

```text
network-observation/
  core/                 domain model (`observation-model` target)
  service/              `ObservationService`, `NetlinkLldpObservationRuntime`, internal ports
    ports/
  inputs/
    netlink/
    lldp/
  transports/
  clients/
  apps/
```

Runtime dependency shape:

- `inputs/* -> core`
- `transports/* -> service/ports <- service`
- `service -> core`
- `apps -> service + selected concrete inputs/transports`
- `clients -> contract + client-side codec/support`

## Consistency policy

`network-observation` deliberately follows the same top-level layer pattern as
`rmc-inventory`, but keeps a more detailed internal split because its domain is
broader. That difference is intentional. By contrast, packaging/export drift,
orphan artifacts, and inconsistent lifecycle mutation rules are treated as
debt, not as service-specific design freedom.

## Architecture

```
┌──────────────────────────────────────────────────────────────────┐
│                      Consumer Application                        │
│                    (rmc-inventoryd / CLI)                        │
└─────────────────────────────┬────────────────────────────────────┘
                              │
                ModelConfig + NetlinkLldpObservationRuntime
                              │
┌─────────────────────────────▼────────────────────────────────────┐
│               observation-model (static library)                 │
│  ┌───────────────────┐  ┌──────────────────┐  ┌────────────────┐ │
│  │ ObservationModel  │  │   HardFilter     │  │  Classifiers   │ │
│  │     Engine        │  │ (multi/IP/mac)   │  │ (Rule/Scoring) │ │
│  └───────────────────┘  └──────────────────┘  └────────────────┘ │
│  ┌───────────────────┐  ┌──────────────────┐                     │
│  │ LocalStateTracker │  │ IInterfacePolicy │                     │
│  └───────────────────┘  └──────────────────┘                     │
└─────────────────────────────┬────────────────────────────────────┘
                              │
                         MonitorCallbacks
                              │
┌─────────────────────────────▼────────────────────────────────────┐
│                netlink-monitor (static library)                  │
│  ┌────────────┐  ┌────────────┐  ┌────────────┐                  │
│  │  Parser    │  │   State    │  │   Utils    │                  │
│  │ (handle*)  │  │ (Netlink   │  │ (format)   │                  │
│  │            │  │  State)    │  │            │                  │
│  └────────────┘  └────────────┘  └────────────┘                  │
└─────────────────────────────┬────────────────────────────────────┘
                              │
                       Netlink socket
                              │
┌─────────────────────────────▼────────────────────────────────────┐
│                       Linux Kernel                               │
│                 RTM_GETLINK / RTM_GETADDR / RTM_GETNEIGH         │
└──────────────────────────────────────────────────────────────────┘
```

## Internal libraries

| Target | Linkage | Purpose |
|---|---|---|
| `netlink-monitor` | Static, internal | Raw netlink event monitor for links, addresses, FDB entries, and neighbors |
| `lldp-observer` | Static, internal | LLDP ingestion and callback-drain handling |
| `observation-model` | Static, internal | Source-agnostic model with filtering, classification, and candidate inference |

These targets are linked into `network-observationd` through
`network_observation_service`. They are not installed, exported, or supported
as independent runtime libraries.
---

## 1. Raw Netlink Monitor

The `netlink-monitor` library provides direct access to Linux netlink events — links, IP addresses, bridge FDB entries, and ARP/NDP neighbors.

### Basic Usage

```cpp
#include "NetlinkNetworkMonitor.h"
#include "NetlinkTypes.h"

using namespace RSCGroup;

// Define callbacks for events you care about
MonitorCallbacks callbacks;

callbacks.onLinkChanged = [](const LinkEvent& e) {
    printf("[link] %s admin=%s running=%s operstate=%u\n",
           e.ifname.c_str(),
           e.adminUp ? "up" : "down",
           e.running ? "yes" : "no",
           e.operState);
};

callbacks.onInterfaceIpChanged = [](const InterfaceIpEvent& e) {
    printf("[ip] %s %s %s/%u\n",
           e.ifname.c_str(),
           e.present ? "added" : "removed",
           e.address.c_str(),
           e.prefixLen);
};

callbacks.onNeighborChanged = [](const NeighborEvent& e) {
    printf("[neigh] %s mac=%s ip=%s nudState=0x%02x\n",
           e.ifname.c_str(),
           e.mac.c_str(),
           e.ip.c_str(),
           e.nudState);
};

callbacks.onFdbChanged = [](const FdbEvent& e) {
    printf("[fdb] port=%s mac=%s local=%s permanent=%s\n",
           e.port.c_str(),
           e.mac.c_str(),
           e.local ? "yes" : "no",
           e.permanent ? "yes" : "no");
};

callbacks.onDeviceChanged = [](const DeviceEvent& e) {
    printf("[device] mac=%s port=%s present=%s\n",
           e.mac.c_str(),
           e.port.c_str(),
           e.present ? "yes" : "no");
};

// Create and start
NetlinkNetworkMonitor monitor(callbacks);
if (!monitor.start()) {
    fprintf(stderr, "Failed to start monitor\n");
    return 1;
}

// Query snapshots at any time
auto links = monitor.getLinksSnapshot();
auto devices = monitor.getDevicesSnapshot();

// Stop
monitor.stop();
```

### Filtering FDB by Interface

```cpp
// Only receive FDB events learned on eth1
std::set<std::string> watchedInterfaces = {"eth1"};
NetlinkNetworkMonitor monitor(callbacks, watchedInterfaces);
```

### LinkEvent Fields

| Field | Description |
|---|---|
| `ifindex` | Kernel interface index |
| `ifname` | Interface name (e.g. `"eth0"`, `"br-lan"`) |
| `mac` | Hardware MAC address |
| `adminUp` | Interface is administratively up |
| `running` | Interface has carrier/running |
| `operState` | RFC 2863 operstate (0=unknown, 6=up) |
| `masterIfindex` | Bridge/bond master ifindex if enslaved |
| `masterIfname` | Bridge/bond master name (e.g. `"br-lan"`) |

### NeighborEvent Fields

| Field | Description |
|---|---|
| `ifname` | Interface the neighbor was learned on |
| `mac` | MAC address |
| `family` | `AF_INET` or `AF_INET6` |
| `ip` | IP address |
| `nudState` | Raw kernel NUD state (0x02=REACHABLE, 0x04=STALE, etc.) |
| `present` | `true` if learned, `false` if removed |

---

## 2. Observation Model

The `core/` domain model is still built as the `observation-model` library and adds:

- **Hard artifact filtering** — rejects multicast MACs, loopback, null MACs, IEEE reserved MACs
- **Interface policy** — controls which interfaces contribute local state or remote candidates
- **Candidate classification** — classifies remote devices (rule-based or scoring)
- **Provisional/confirmed lifecycle** — devices are provisional during startup, confirmed after baseline
- **Publishability gate** — only confirmed, non-artifact candidates are returned

### Preferred Usage — `NetlinkLldpObservationRuntime`

```cpp
#include "NetlinkLldpObservationRuntime.h"
#include "ModelConfig.h"
#include "INetworkObservationModel.h"

using namespace RSCGroup;

// --- Option A: Use default engine with ModelConfig ---

ModelConfig config;
config.skipNullMac = true;          // reject 00:00:00:00:00:00
config.skipMulticastMac = true;     // reject 01:xx:xx / 33:33:xx
config.skipMulticastIPv4 = true;    // reject 224.0.0.0/4
config.skipMulticastIPv6 = true;    // reject ff00::/8
config.skipIeeeReservedMac = true;  // reject 01:80:c2:xx:xx:xx
config.candidateAgeout = std::chrono::seconds(60);
config.candidateExpire = std::chrono::seconds(300);

NetlinkLldpObservationRuntime adapter(std::move(config));
adapter.start();

// Query local state
auto local = adapter.localSnapshot();
for (const auto& [name, iface] : local.interfaces) {
    printf("Interface: %s mac=%s ipv4=%zu ipv6=%zu\n",
           name.c_str(), iface.mac.c_str(),
           iface.ipv4.size(), iface.ipv6.size());
}

// Query remote candidates (only publishable ones)
auto candidates = adapter.remoteCandidates();
for (const auto& c : candidates) {
    printf("Device: mac=%s port=%s ipv4=%s classification=%d\n",
           c.mac.c_str(),
           c.bridgePort.value_or("?").c_str(),
           c.ipv4.empty() ? "none" : c.ipv4.begin()->c_str(),
           static_cast<int>(c.classification));
}

// Find a specific candidate
auto dev = adapter.findCandidateByMac("74:56:3c:08:1e:b9");
if (dev) {
    printf("Found: mac=%s seenInFdb=%d seenInNeigh=%d\n",
           dev->mac.c_str(), dev->seenInFdb, dev->seenInNeigh);
}

adapter.stop();
```

### ModelConfig Fields

| Field | Type | Default | Description |
|---|---|---|---|
| `skipNullMac` | `bool` | `true` | Reject MAC `00:00:00:00:00:00` (loopback placeholder) |
| `skipMulticastMac` | `bool` | `true` | Reject MACs with multicast bit set: `01:xx:xx` / `33:33:xx` |
| `skipMulticastIPv4` | `bool` | `true` | Reject IPv4 multicast range `224.0.0.0/4` |
| `skipMulticastIPv6` | `bool` | `true` | Reject IPv6 multicast range `ff00::/8` |
| `skipIeeeReservedMac` | `bool` | `true` | Reject IEEE reserved MAC range `01:80:c2:xx:xx:xx` |
| `skipLoopbackInterface` | `bool` | `true` | Reject observations on loopback interface `lo` |
| `emitDeviceIfIpOnly` | `bool` | `false` | If `true`, emit device events for IP-only entries with no FDB |
| `candidateAgeout` | `seconds` | `60` | Interval after which a candidate is marked `Aged` |
| `candidateExpire` | `seconds` | `300` | Interval after which a candidate is marked `Expired` |
| `interfacePolicy` | `unique_ptr<IInterfacePolicy>` | `null` | Custom interface filter policy; defaults to `DefaultInterfacePolicy` |
| `classifierConfig` | `ClassifierFactoryConfig` | `RuleBased` | Classifier kind selection (`RuleBased` or `Scoring`) |

### Advanced — Custom Model Implementation

```cpp
class MyModel : public INetworkObservationModel {
    // ... implement all pure virtual methods ...
};

auto myModel = std::make_unique<MyModel>();
NetlinkLldpObservationRuntime adapter(std::move(myModel));
adapter.start();
```

### Runtime Configuration Changes

```cpp
NetlinkLldpObservationRuntime adapter(ModelConfig{});

// Swap classifier at runtime (thread-safe, no restart)
adapter.setClassifier(std::make_unique<ScoringClassifier>(30, 60, 90));

// Swap interface policy at runtime
adapter.setInterfacePolicy(std::make_unique<DefaultInterfacePolicy>());
```

### Custom Interface Policy

```cpp
class MyPolicy : public DefaultInterfacePolicy {
public:
    bool allowRemoteFdbEvidence(std::string_view ifname) const override {
        // Only accept FDB from eth0 and eth1
        if (ifname == "eth0" || ifname == "eth1") return true;
        return false;
    }
};

ModelConfig config;
config.interfacePolicy = std::make_unique<MyPolicy>();
NetlinkLldpObservationRuntime adapter(std::move(config));
```

### RemoteCandidate Fields

| Field | Description |
|---|---|
| `mac` | MAC address (primary key) |
| `ipv4` | Set of IPv4 addresses seen in neighbor entries |
| `ipv6` | Set of IPv6 addresses seen in neighbor entries |
| `bridgePort` | Bridge port from FDB (e.g. `"eth1"`) |
| `neighborIfaces` | Interfaces where this MAC was seen in ARP/NDP |
| `seenInFdb` | Evidence from bridge FDB |
| `seenInNeigh` | Evidence from ARP/NDP neighbor table |
| `seenInLldp` | Evidence from LLDP (future) |
| `classification` | `Artifact`, `LocalSelf`, `WeakCandidate`, `RemoteEndpoint`, `GatewayLike`, `TopologyPeer`, `Unknown` |
| `status` | `Provisional`, `Confirmed`, `Aged`, `Expired`, `Removed` |
| `firstSeen` / `lastSeen` | Timestamps |

### CandidateClassification

| Value | Meaning |
|---|---|
| `Artifact` | Multicast, control-plane MAC — not a device |
| `LocalSelf` | MAC or IP belongs to a local interface |
| `WeakCandidate` | Insufficient evidence to confidently classify |
| `RemoteEndpoint` | Neighbor + FDB evidence, behind a bridge port |
| `GatewayLike` | Also matches default gateway IP |
| `TopologyPeer` | LLDP-confirmed physical peer |
| `Unknown` | Not yet classified |

### Build

```cmake
# From your project's CMakeLists.txt
add_subdirectory(network-observation)

target_link_libraries(your_app netlink-monitor observation-model)
```

### What Gets Filtered (Defaults)

With default `ModelConfig`, the following are automatically rejected:

| Artifact Type | Examples |
|---|---|
| Null MAC | `00:00:00:00:00:00` |
| Loopback | `127.0.0.1`, `::1` on `lo` |
| IPv4 multicast | `224.0.0.251` (mDNS), `224.0.0.1` (All-Hosts) |
| IPv6 multicast | `ff02::1`, `ff02::2`, `ff02::16`, `ff02::fb`, `ff02::1:ffxx:xxxx` |
| Multicast MACs | `01:00:5e:xx:xx:xx` (IPv4-mapped), `33:33:xx:xx:xx:xx` (IPv6-mapped) |
| IEEE reserved | `01:80:c2:00:00:00` (STP), `01:80:c2:00:00:03` (STP), `01:80:c2:00:00:0e` (LLDP) |
| Local MACs/IPs | Any MAC or IP belonging to the host's own interfaces |

## Deployment: Buildroot + SystemV + Monit

This service is intended to run in a Buildroot-based system with:

- system D-Bus
- Monit as the service supervisor
- wrapper scripts for start/stop/health checks
- no required SysV init service script for `network-observationd`

### Service management model

`network-observationd` is managed directly by **Monit**.

Monit is responsible for:

- starting the daemon
- stopping the daemon
- restarting the daemon on failure
- health monitoring
- enable/disable supervision (`monitor` / `unmonitor`)

Rather than using `/etc/init.d/network-observationd`, this deployment uses small wrapper scripts installed under:

```text
/usr/libexec/network-observation/
  start-network-observationd
  stop-network-observationd
  check-network-observationd
```

This keeps service lifecycle logic in one place while still allowing Monit to be the only control plane.

### Runtime filesystem layout

```text
/usr/sbin/network-observationd
/usr/bin/net-observe

/etc/network-observation/network-observationd.conf
/etc/monit.d/network-observationd.monitrc

/usr/libexec/network-observation/start-network-observationd
/usr/libexec/network-observation/stop-network-observationd
/usr/libexec/network-observation/check-network-observationd

/etc/dbus-1/system.d/org.rsc.NetworkObservation.conf

/run/network-observation/network-observationd.pid
/var/log/network-observationd.log
```

### Source tree layout for deployment assets

```text
services/network-observation/
  packaging/
    monit/
      network-observationd.monitrc
    scripts/
      start-network-observationd
      stop-network-observationd
      check-network-observationd
    config/
      network-observationd.conf
      network-observationd.conf.example
    dbus/
      org.rsc.NetworkObservation.conf
```

### Configuration file

The runtime configuration file is:

```text
/etc/network-observation/network-observationd.conf
```

It is a shell-style `KEY=VALUE` file sourced by the wrapper scripts.

Example:

```sh
DAEMON=/usr/sbin/network-observationd
CLI=/usr/bin/net-observe
RUNDIR=/run/network-observation
PIDFILE=/run/network-observation/network-observationd.pid
LOGFILE=/var/log/network-observationd.log
TRANSPORT=dbus
TRANSPORT_CONFIG=system
STARTUP_WAIT_SEC=20
STOP_WAIT_SEC=20
DBUS_REPLY_TIMEOUT_MS=3000
```

### Startup behavior

The `start-network-observationd` wrapper:

1. loads `/etc/network-observation/network-observationd.conf`
2. waits for the system D-Bus socket to become available
3. creates `/run/network-observation`
4. starts `network-observationd`
5. writes a pidfile
6. polls `org.rsc.NetworkObservation.GetReady` over D-Bus
7. fails the startup if the daemon does not become ready in time

### Stop behavior

The `stop-network-observationd` wrapper:

1. reads the pidfile
2. sends `SIGTERM`
3. waits for a bounded timeout
4. sends `SIGKILL` if needed
5. removes the pidfile

### Health check behavior

The `check-network-observationd` wrapper verifies:

- pidfile exists
- process is alive
- D-Bus method `GetReady` succeeds
- `GetReady` returns `true`

This means Monit checks actual application readiness, not just process existence.

### Monit configuration

Monit should supervise the daemon using:

- pidfile-based process monitoring
- wrapper-based start/stop methods
- D-Bus-aware health checks

Example:

```monit
check process network-observationd with pidfile /run/network-observation/network-observationd.pid
    start program = "/usr/libexec/network-observation/start-network-observationd" with timeout 60 seconds
    stop program  = "/usr/libexec/network-observation/stop-network-observationd" with timeout 60 seconds

    if failed program "/usr/libexec/network-observation/check-network-observationd" then restart
    if 5 restarts within 10 cycles then unmonitor

    group network-observation
```

### Manual operations

With this model, service control should go through Monit:

```sh
monit start network-observationd
monit stop network-observationd
monit restart network-observationd
monit status network-observationd
monit unmonitor network-observationd
monit monitor network-observationd
```

### D-Bus policy

A system-bus policy file must be installed to allow the service to own its bus name and accept requests:

```text
/etc/dbus-1/system.d/org.rsc.NetworkObservation.conf
```

This deployment does **not** require D-Bus activation. The daemon is expected to be started explicitly by Monit, not lazily by the bus.

### Why this model

This Monit-only approach is a good fit for appliance-style Buildroot systems because it:

- keeps enable/disable behavior in one tool
- avoids split lifecycle ownership between init and Monit
- provides D-Bus-aware health monitoring
- keeps startup/stop logic explicit and scriptable
