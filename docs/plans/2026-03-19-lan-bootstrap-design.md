# LAN Bootstrap Mode Design

## Goal

Add a LAN-oriented bootstrap mode that lets both ToxTunnel server and client operate inside the
same local network without depending on `https://nodes.tox.chat/json`, while still allowing
manually configured bootstrap nodes as supplemental peers.

## Background

ToxTunnel currently treats public bootstrap discovery as the default network-entry path. When no
bootstrap nodes are configured, `ToxAdapter::bootstrap()` resolves nodes through
`BootstrapSource`, which fetches `https://nodes.tox.chat/json`, caches the result under
`data_dir/bootstrap_nodes.json`, and then bootstraps against those nodes.

That behavior works for Internet-connected deployments, but it is a poor fit for purely local
networks:

1. Startup depends on an external public service even when both peers are on the same LAN.
2. Air-gapped or restricted intranet deployments cannot rely on `nodes.tox.chat`.
3. The current config shape spreads Tox network settings between server-only fields and implicit
   client defaults, making it hard to introduce shared bootstrap behavior cleanly.

Bundled `c-toxcore` already supports local network peer discovery through
`tox_options_set_local_discovery_enabled(...)`. That gives us a built-in zero-configuration LAN
bootstrap mechanism for peers on the same broadcast domain. The missing piece is exposing this as
an intentional ToxTunnel mode and restructuring config so both client and server can use it.

## Problem Statement

We need a bootstrap mode that:

- works for both `server` and `client`,
- does not require `nodes.tox.chat`,
- keeps manual bootstrap nodes available as optional supplements,
- preserves current behavior for existing Internet-oriented deployments,
- and introduces a cleaner configuration model for future Tox networking options.

## Requirements

### Functional Requirements

1. Add a new bootstrap mode for LAN usage.
2. Support the new mode in both server and client operation.
3. In LAN mode, enable toxcore local discovery and avoid any dependency on public bootstrap fetch.
4. In LAN mode, still allow explicitly configured bootstrap nodes to be used as supplements.
5. Keep current automatic public bootstrap behavior as the default when LAN mode is not selected.

### Configuration Requirements

1. Introduce a top-level shared `tox` configuration block.
2. Move Tox networking options into that block:
   - `udp_enabled`
   - `tcp_port`
   - `bootstrap_mode`
   - `bootstrap_nodes`
3. Keep old configuration fields readable for backward compatibility, but serialize only the new
   canonical shape.

### Non-Functional Requirements

1. Do not add a background bootstrap refresh loop.
2. Do not redesign the CLI in this change.
3. Keep bootstrap behavior deterministic and easy to reason about.
4. Avoid regressions in existing public-network deployments.

## Constraints and Assumptions

- toxcore local discovery only helps peers on the same local broadcast domain. It is not a
  replacement for routed, WAN, or segmented-network bootstrap.
- LAN mode depends on UDP being enabled. A LAN mode configuration with UDP disabled is invalid.
- Manually configured bootstrap nodes remain useful in LAN mode for environments with an internal
  bootstrap daemon or for mixed deployments where local discovery alone is insufficient.
- `tcp_port` remains meaningful only for server mode, even though it is represented in the shared
  `tox` section.

## Options Considered

### Option 1: Duplicate `bootstrap_mode` in `server` and `client`

Pros:

- Minimal change to current config structure.
- Smallest implementation diff in application code.

Cons:

- Splits one Tox concern across two mode-specific sections.
- Makes future shared toxcore options harder to evolve cleanly.
- Requires duplicated parsing and normalization logic.

### Option 2: Add a top-level shared `tox` section

Pros:

- Models Tox network settings where they logically belong.
- Gives client and server one consistent bootstrap configuration surface.
- Simplifies future extension of Tox-specific options.

Cons:

- Requires config migration and compatibility handling.
- Touches more parsing and serialization code in the short term.

### Option 3: Add a single boolean such as `lan_bootstrap: true`

Pros:

- Smallest user-facing syntax change.
- Fastest to implement.

Cons:

- Too narrow for long-term evolution.
- Does not solve the current split between shared and mode-specific Tox settings.
- Becomes awkward if more bootstrap modes are added later.

## Decision

Adopt **Option 2** and introduce a top-level `tox` configuration section as the canonical home for
shared Tox network settings.

This is the most structurally sound choice. It solves the immediate LAN bootstrap requirement while
also cleaning up the existing configuration model, which currently treats some Tox settings as
server-only and hardcodes others in the client path.

## Proposed Configuration Model

### Canonical YAML Shape

```yaml
mode: server
data_dir: /var/lib/toxtunnel

logging:
  level: info

tox:
  udp_enabled: true
  tcp_port: 33445
  bootstrap_mode: lan
  bootstrap_nodes:
    - address: 192.168.1.10
      port: 33445
      public_key: "AABBCC..."

server:
  rules_file: /etc/toxtunnel/rules.yaml
```

```yaml
mode: client
data_dir: ~/.config/toxtunnel

logging:
  level: info

tox:
  udp_enabled: true
  bootstrap_mode: lan
  bootstrap_nodes: []

client:
  server_id: "AABBCC..."
  forwards:
    - local_port: 2222
      remote_host: 127.0.0.1
      remote_port: 22
```

### New Types

- `enum class BootstrapMode { Auto, Lan };`
- `struct ToxConfig { ... }`

`ToxConfig` contains:

- `bool udp_enabled = true;`
- `uint16_t tcp_port = 33445;`
- `BootstrapMode bootstrap_mode = BootstrapMode::Auto;`
- `std::vector<BootstrapNodeConfig> bootstrap_nodes;`

`Config` gains a top-level `tox` member and treats it as the canonical source of shared Tox
network configuration.

## Behavioral Semantics

### `bootstrap_mode: auto`

This preserves current behavior:

1. If `tox.bootstrap_nodes` is non-empty, use those nodes directly.
2. Otherwise fetch `https://nodes.tox.chat/json`.
3. Cache successful results under `data_dir/bootstrap_nodes.json`.
4. Fall back to cached nodes when remote fetch fails.

### `bootstrap_mode: lan`

This is the new LAN-oriented behavior:

1. Enable toxcore local discovery explicitly.
2. Do not fetch `https://nodes.tox.chat/json`.
3. Do not read or write the public bootstrap cache.
4. If `tox.bootstrap_nodes` is non-empty, use those nodes as supplements.
5. If `tox.bootstrap_nodes` is empty, rely entirely on local discovery.

### Supplemental Manual Nodes in LAN Mode

Manual nodes are not mutually exclusive with local discovery. In LAN mode they are additive:

- local discovery still runs,
- configured nodes are still passed through `tox_bootstrap(...)`,
- configured nodes are still added through `tox_add_tcp_relay(...)`.

This allows LAN mode to support:

- zero-configuration peer discovery on a small local network,
- internal bootstrap daemons,
- and mixed local/private environments where extra seed nodes improve reliability.

## Detailed Implementation

### 1. Config Layer

Files:

- `include/toxtunnel/util/config.hpp`
- `src/util/config.cpp`
- `tests/unit/test_config.cpp`
- `tests/integration/test_config_pipeline.cpp`

Changes:

1. Add `BootstrapMode` enum and `ToxConfig` struct.
2. Add `tox` to `Config`.
3. Update defaults:
   - `Config::default_server()` populates `tox` with default values.
   - `Config::default_client()` populates `tox` with default values.
4. Update validation:
   - `tox.tcp_port` must be non-zero when used in server mode.
   - `tox.bootstrap_mode == lan` requires `tox.udp_enabled == true`.
   - existing bootstrap public-key validation moves under `tox.bootstrap_nodes`.
5. Update serialization:
   - `to_yaml()` emits only canonical `tox:` fields.
6. Update YAML conversion:
   - add `YAML::convert<BootstrapMode>`
   - add `YAML::convert<ToxConfig>`
7. Add compatibility parsing:
   - continue reading legacy flat keys:
     - `tcp_port`
     - `udp_enabled`
     - `bootstrap_nodes`
   - continue reading legacy nested server keys:
     - `server.tcp_port`
     - `server.udp_enabled`
     - `server.bootstrap_nodes`
   - normalize all legacy values into `Config::tox`.

Compatibility precedence:

1. Canonical `tox.*` wins if present.
2. Otherwise use legacy nested server values.
3. Otherwise use legacy flat values.
4. Defaults fill any remaining gaps.

This keeps old configs working while defining one clear serialization target.

### 2. Application Layer

Files:

- `src/app/tunnel_server.cpp`
- `src/app/tunnel_client.cpp`

Changes:

1. `TunnelServer` builds `ToxAdapterConfig` from `config.tox` instead of `server_cfg`.
2. `TunnelClient` also builds `ToxAdapterConfig` from `config.tox`.
3. `server.rules_file` remains server-specific.
4. `client.server_id`, `client.forwards`, and `client.pipe_target` remain client-specific.
5. `tox.tcp_port` is applied only in server mode when constructing the adapter config.

This keeps mode-specific logic in the application layer while centralizing network behavior.

### 3. Tox Adapter Configuration

Files:

- `include/toxtunnel/tox/tox_adapter.hpp`
- `src/tox/tox_adapter.cpp`

Changes:

1. Extend `ToxAdapterConfig` with:
   - `BootstrapMode bootstrap_mode`
   - `bool local_discovery_enabled`
2. In `initialize()` call:
   - `tox_options_set_udp_enabled(...)`
   - `tox_options_set_local_discovery_enabled(...)`
3. Keep `tcp_port` optional and apply it only when non-zero.

Mode mapping:

- `bootstrap_mode == auto`
  - `local_discovery_enabled = false`
- `bootstrap_mode == lan`
  - `local_discovery_enabled = true`

Rationale:

Even though toxcore defaults local discovery to `true`, ToxTunnel should set the behavior
explicitly so the chosen mode is visible in code and does not depend on upstream defaults.

### 4. Bootstrap Source

Files:

- `include/toxtunnel/tox/bootstrap_source.hpp`
- `src/tox/bootstrap_source.cpp`
- `tests/unit/test_bootstrap_source.cpp`

Changes:

1. Change bootstrap resolution to depend on `BootstrapMode`.
2. In `auto` mode:
   - preserve existing fetch and cache behavior.
3. In `lan` mode:
   - skip remote fetch,
   - skip cache read,
   - return only explicitly configured bootstrap nodes.

Possible interface shape:

```cpp
resolve_bootstrap_nodes(const std::vector<BootstrapNode>& configured_nodes,
                        BootstrapMode mode,
                        const std::filesystem::path& data_dir,
                        Fetcher fetcher = {},
                        std::size_t max_nodes = kDefaultMaxNodes)
```

This keeps mode policy isolated in the bootstrap source helper instead of spreading remote-fetch
decisions across application code.

### 5. Runtime Bootstrap Flow

Files:

- `src/tox/tox_adapter.cpp`

Updated `ToxAdapter::bootstrap()` flow:

1. Start with `config_.bootstrap_nodes`.
2. Resolve additional nodes through `BootstrapSource` according to `config_.bootstrap_mode`.
3. If mode is `lan` and no manual nodes are configured, allow the bootstrap list to remain empty.
4. Continue iterating toxcore so local discovery can establish connectivity.
5. For any returned nodes:
   - call `tox_bootstrap(...)`
   - call `tox_add_tcp_relay(...)`

Logging changes:

- Avoid misleading warnings in LAN mode when no manual bootstrap nodes are present.
- Distinguish between:
  - "public bootstrap discovery failed" in `auto` mode,
  - and "LAN mode enabled; relying on local discovery" in `lan` mode.

This is important because `0` explicit bootstrap contacts is expected in pure LAN mode.

### 6. Documentation

Files:

- `docs/CONFIGURATION.md`
- `README.md`
- `docs/ARCHITECTURE.md`

Documentation changes:

1. Introduce canonical `tox:` configuration examples.
2. Document `bootstrap_mode: auto | lan`.
3. Explain LAN mode limitations:
   - same LAN or broadcast domain,
   - UDP required,
   - manual nodes optional but helpful.
4. Document legacy config compatibility as supported input, but not canonical output.
5. Update architecture docs to reflect that Tox network options now live in `Config::tox`.

## Compatibility and Migration

### Backward Compatibility

Existing configs continue to work:

- flat server-style fields remain readable,
- nested `server.*` bootstrap fields remain readable,
- existing public bootstrap behavior remains the default.

No behavior changes for existing users unless they opt into `bootstrap_mode: lan` or migrate to
the new `tox:` block.

### Forward Compatibility

The top-level `tox` block creates a clean place for future shared options, such as:

- IPv6 policy,
- hole punching toggles,
- proxy-related Tox settings,
- additional bootstrap strategies.

### Migration Guidance

Old:

```yaml
mode: server
data_dir: /var/lib/toxtunnel

server:
  tcp_port: 33445
  udp_enabled: true
  bootstrap_nodes:
    - address: node1.tox.chat
      port: 33445
      public_key: "AABBCC..."
```

New canonical form:

```yaml
mode: server
data_dir: /var/lib/toxtunnel

tox:
  tcp_port: 33445
  udp_enabled: true
  bootstrap_mode: auto
  bootstrap_nodes:
    - address: node1.tox.chat
      port: 33445
      public_key: "AABBCC..."

server: {}
```

LAN-oriented form:

```yaml
mode: server
data_dir: /var/lib/toxtunnel

tox:
  tcp_port: 33445
  udp_enabled: true
  bootstrap_mode: lan
  bootstrap_nodes: []

server: {}
```

## Testing Strategy

### Unit Tests

1. Config parsing and validation:
   - parse canonical `tox:` config for server and client,
   - parse legacy config and verify normalization into `Config::tox`,
   - reject `bootstrap_mode: lan` with `udp_enabled: false`,
   - verify `to_yaml()` emits canonical `tox:` output only.
2. Bootstrap source behavior:
   - `auto` mode uses explicit nodes when present,
   - `auto` mode fetches and falls back to cache,
   - `lan` mode skips remote fetch,
   - `lan` mode skips cache fallback,
   - `lan` mode returns explicit nodes unchanged when provided.

### Integration Tests

1. Config pipeline tests covering full canonical client/server YAML.
2. Config pipeline tests covering legacy-to-canonical compatibility.
3. If practical, adapter bootstrap-flow tests that verify mode-specific logging and node
   resolution behavior without requiring external network access.

### Manual Verification

1. Start server and client on the same LAN with:
   - `tox.bootstrap_mode: lan`
   - no `tox.bootstrap_nodes`
2. Confirm neither side attempts to fetch `https://nodes.tox.chat/json`.
3. Confirm peers connect through local discovery.
4. Repeat with one or more manual internal bootstrap nodes configured and verify that both local
   discovery and explicit bootstrap coexist.

## Risks

1. Config compatibility complexity increases because we will support canonical and legacy shapes at
   the same time.
2. LAN mode can appear non-functional if users expect it to work across routed networks where
   local broadcast discovery does not propagate.
3. Logging must be updated carefully so pure LAN mode does not look like a bootstrap failure.

## Mitigations

1. Centralize config normalization in `Config` rather than scattering legacy handling in the
   application layer.
2. Document LAN scope and UDP requirement clearly in config docs and README examples.
3. Add explicit tests for mode semantics in `BootstrapSource` and config validation.

## Non-Goals

- No new CLI switch such as `--lan` in this change.
- No background bootstrap refresh loop.
- No multicast/rendezvous system beyond toxcore local discovery.
- No removal of legacy config parsing in this change.

## Summary

The chosen design introduces a top-level canonical `tox` config section and a new
`bootstrap_mode: lan` setting that enables toxcore local discovery for both client and server.
This removes the current hard dependency on `nodes.tox.chat` for LAN-only deployments, keeps
manual bootstrap nodes available as supplements, and preserves existing public-bootstrap behavior
for current users.
