# SSH Over ToxTunnel Repair Design

## Goal

Restore real-world SSH usability for ToxTunnel by fixing the broken friend-connection path,
making the documented YAML shape valid, adding native stdio pipe mode for SSH `ProxyCommand`,
and defaulting bootstrap discovery to `https://nodes.tox.chat/json` when the user does not
provide explicit nodes.

## Current Problems

1. The README documents nested `server:` and `client:` sections, but the config loader only
   reads flat top-level keys.
2. The server handles incoming friend requests from inside the tox iterate callback path and
   re-enters `ToxAdapter` methods that take the same mutex, which can stall acceptance and
   leave the client permanently offline.
3. The CLI does not implement a real pipe mode, so the documented SSH `ProxyCommand` example
   cannot work.
4. Default configs contain no bootstrap nodes, so first-run connectivity depends on external
   state rather than a deterministic default.

## Design Summary

### 1. Config Compatibility

The config loader will accept both formats:

- Existing flat format:
  - `tcp_port`, `udp_enabled`, `bootstrap_nodes`, `rules_file`
  - `server_id`, `forwards`
- Documented nested format:
  - `server.tcp_port`, `server.udp_enabled`, `server.bootstrap_nodes`, `server.rules_file`
  - `client.server_id`, `client.forwards`

Serialization will continue to emit the nested format so docs and generated YAML match.
Validation semantics remain the same except that the parsed values now come from either source.

### 2. Default Bootstrap Discovery

When no bootstrap nodes are configured explicitly, ToxTunnel will fetch nodes from
`https://nodes.tox.chat/json`.

Selection rules:

- Prefer nodes with `status_udp == true`.
- Use `ipv4` when available, otherwise `ipv6`.
- Ignore nodes missing host, port, or valid public key.
- Keep the result capped to a small reasonable list for startup.

Resilience rules:

- Successful fetches are cached under the application data directory.
- If the remote fetch fails, cached nodes are used.
- If both fetch and cache are unavailable, startup continues with an empty bootstrap list and
  logs a warning instead of crashing.
- Explicitly configured `bootstrap_nodes` always take precedence and disable remote discovery.

### 3. Callback Deadlock Removal

`ToxAdapter::run_loop()` currently calls `tox_iterate()` while holding `tox_mutex_`, and
toxcore callbacks can synchronously call back into adapter methods that also lock
`tox_mutex_`. The fix is to stop invoking user callbacks under that lock.

The adapter will:

- capture toxcore callback data into lightweight event records,
- append them to an internal event queue while inside the tox callback,
- drain and dispatch those events after `tox_iterate()` returns and after `tox_mutex_` is
  released.

This keeps toxcore access serialized while allowing higher-level callbacks such as
`TunnelServer::on_friend_request()` to call adapter methods safely.

### 4. SSH Pipe Mode

Add a client-side pipe mode for SSH `ProxyCommand`.

User-facing behavior:

- `toxtunnel -m client --server-id <id> --pipe host:port`

Runtime behavior:

- client starts Tox as usual,
- waits for the server friend to come online,
- opens one tunnel to `host:port`,
- bridges process `stdin` to the tunnel and tunnel data to `stdout`,
- exits when stdin closes, the tunnel closes, or an error occurs.

Implementation approach:

- add a small `StdioPipeBridge` helper that owns asynchronous descriptors for stdin/stdout,
- reuse the existing `TunnelImpl` and `TunnelManager` data path,
- do not start `TcpListener` instances in pipe mode,
- preserve existing port-forward mode unchanged.

### 5. CLI and Documentation

CLI additions:

- `--pipe host:port` for client stdio mode

CLI semantics cleanup:

- `-p/--port` remains server TCP relay port override only
- SSH `ProxyCommand` examples switch to `--pipe %h:%p`

README updates:

- nested YAML examples stay as the canonical format,
- bootstrap discovery is documented as automatic by default,
- SSH examples use the new pipe mode,
- port-forward examples remain supported.

## Testing Strategy

1. Config tests for nested YAML parse/round-trip compatibility.
2. Unit tests for bootstrap node fetch parsing and cache fallback.
3. Unit tests for adapter event dispatch behavior that would have deadlocked before.
4. Integration tests for pipe-mode bridge logic using in-process streams/mocks.
5. Full build and test suite.
6. Manual end-to-end verification:
   - local temporary `sshd`
   - server/client startup
   - `ssh -o ProxyCommand="toxtunnel ... --pipe %h:%p"` succeeds

## Non-Goals

- No full CLI subcommand redesign in this change.
- No SOCKS5/dynamic forwarding support.
- No background bootstrap refresh loop after startup.
