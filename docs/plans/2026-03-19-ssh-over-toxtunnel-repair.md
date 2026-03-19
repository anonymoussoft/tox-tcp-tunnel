# SSH Over ToxTunnel Repair Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Restore working SSH over ToxTunnel by fixing config compatibility, deadlock-prone
Tox callback dispatch, dynamic bootstrap defaults, and native stdio pipe mode for SSH
`ProxyCommand`.

**Architecture:** Keep the existing client/server/tunnel architecture intact. Add a small
bootstrap discovery helper, make adapter callbacks queue-and-dispatch outside the tox mutex,
extend client mode with a single-tunnel stdio bridge, and broaden config parsing so the
documented YAML shape is accepted without breaking flat configs.

**Tech Stack:** C++20, Asio, yaml-cpp, c-toxcore, Google Test

---

### Task 1: Protect Existing Behavior With Failing Config Tests

**Files:**
- Modify: `tests/unit/test_config.cpp`
- Modify: `tests/integration/test_config_pipeline.cpp`

**Step 1: Write the failing test**

Add tests that load nested server/client YAML and expect:

- `server.tcp_port`, `server.udp_enabled`, `server.bootstrap_nodes` parse correctly
- `client.server_id`, `client.forwards` parse correctly
- top-level flat config still parses

**Step 2: Run test to verify it fails**

Run: `./build/tests/unit_tests --gtest_filter=ConfigTest.ParseNested*`

Expected: FAIL because nested keys are ignored today

**Step 3: Write minimal implementation**

Teach `YAML::convert<Config>::decode` and encode/`to_yaml()` paths to accept nested sections and
emit nested sections.

**Step 4: Run test to verify it passes**

Run: `./build/tests/unit_tests --gtest_filter=ConfigTest.ParseNested*`

Expected: PASS

### Task 2: Add Failing Tests For Default Bootstrap Discovery

**Files:**
- Create: `tests/unit/test_bootstrap_source.cpp`
- Modify: `tests/CMakeLists.txt`

**Step 1: Write the failing test**

Add tests for a new bootstrap-source helper that:

- parses `nodes.tox.chat/json` payloads,
- prefers online UDP nodes,
- falls back to cache when remote fetch fails,
- leaves explicit config untouched.

**Step 2: Run test to verify it fails**

Run: `./build/tests/unit_tests --gtest_filter=BootstrapSourceTest*`

Expected: FAIL because the helper does not exist yet

**Step 3: Write minimal implementation**

Create a bootstrap discovery utility with pure parsing/filtering logic first, then add file cache
read/write behavior.

**Step 4: Run test to verify it passes**

Run: `./build/tests/unit_tests --gtest_filter=BootstrapSourceTest*`

Expected: PASS

### Task 3: Add A Regression Test For Safe Callback Dispatch

**Files:**
- Modify: `tests/unit/test_tunnel_manager.cpp` or create `tests/unit/test_tox_adapter.cpp`
- Modify: `tests/CMakeLists.txt`

**Step 1: Write the failing test**

Add a test that simulates callback dispatch re-entering adapter methods and asserts dispatch
occurs after the tox mutex is no longer held.

**Step 2: Run test to verify it fails**

Run: `./build/tests/unit_tests --gtest_filter=ToxAdapterTest.DispatchesCallbacksOutsideInternalLocks`

Expected: FAIL because callbacks currently run directly from the tox callback path

**Step 3: Write minimal implementation**

Add an event queue to `ToxAdapter`, push callback payloads from static tox trampolines, and drain
them after `tox_iterate()` returns.

**Step 4: Run test to verify it passes**

Run: `./build/tests/unit_tests --gtest_filter=ToxAdapterTest.DispatchesCallbacksOutsideInternalLocks`

Expected: PASS

### Task 4: Add Failing Tests For Pipe Mode Argument Handling

**Files:**
- Create: `tests/unit/test_pipe_mode.cpp`
- Modify: `tests/CMakeLists.txt`

**Step 1: Write the failing test**

Add tests for:

- parsing `host:port` into a structured pipe target
- rejecting malformed targets
- client config validation in pipe mode without forward listeners

**Step 2: Run test to verify it fails**

Run: `./build/tests/unit_tests --gtest_filter=PipeModeTest*`

Expected: FAIL because pipe mode types and validation do not exist yet

**Step 3: Write minimal implementation**

Add pipe mode config/CLI parsing helpers and validation rules.

**Step 4: Run test to verify it passes**

Run: `./build/tests/unit_tests --gtest_filter=PipeModeTest*`

Expected: PASS

### Task 5: Add A Bridge Test For Stdio-To-Tunnel Data Flow

**Files:**
- Create: `tests/integration/test_pipe_bridge.cpp`
- Modify: `tests/CMakeLists.txt`

**Step 1: Write the failing test**

Add an integration test that wires a bridge to a `TunnelImpl` and verifies:

- stdin data becomes tunnel outbound data
- inbound tunnel data reaches stdout
- EOF closes the tunnel cleanly

**Step 2: Run test to verify it fails**

Run: `./build/tests/integration_tests --gtest_filter=PipeBridgeTest*`

Expected: FAIL because the bridge does not exist

**Step 3: Write minimal implementation**

Add a small `StdioPipeBridge` helper under `include/toxtunnel/app/` and `src/app/`, then wire
it into client startup.

**Step 4: Run test to verify it passes**

Run: `./build/tests/integration_tests --gtest_filter=PipeBridgeTest*`

Expected: PASS

### Task 6: Implement Runtime Bootstrap Discovery

**Files:**
- Create: `include/toxtunnel/tox/bootstrap_source.hpp`
- Create: `src/tox/bootstrap_source.cpp`
- Modify: `include/toxtunnel/tox/tox_adapter.hpp`
- Modify: `src/app/tunnel_client.cpp`
- Modify: `src/app/tunnel_server.cpp`
- Modify: `CMakeLists.txt`

**Step 1: Integrate the helper**

Use configured bootstrap nodes when provided; otherwise load from remote source/cache before
calling `ToxAdapter::initialize()`.

**Step 2: Run targeted tests**

Run: `./build/tests/unit_tests --gtest_filter=BootstrapSourceTest*`

Expected: PASS

**Step 3: Verify no regressions**

Run: `./build/tests/unit_tests --gtest_filter=ConfigTest*`

Expected: PASS

### Task 7: Implement Safe Tox Callback Dispatch

**Files:**
- Modify: `include/toxtunnel/tox/tox_adapter.hpp`
- Modify: `src/tox/tox_adapter.cpp`

**Step 1: Add event queue plumbing**

Define internal event variants for friend request, connection status, packets, messages, and self
connection updates.

**Step 2: Dispatch outside the tox mutex**

Drain queued events after each iterate cycle and invoke user callbacks without holding
`tox_mutex_`.

**Step 3: Run targeted tests**

Run: `./build/tests/unit_tests --gtest_filter=ToxAdapterTest*`

Expected: PASS

### Task 8: Implement Client Pipe Mode

**Files:**
- Modify: `include/toxtunnel/util/config.hpp`
- Modify: `src/util/config.cpp`
- Modify: `cli/main.cpp`
- Modify: `include/toxtunnel/app/tunnel_client.hpp`
- Modify: `src/app/tunnel_client.cpp`
- Create: `include/toxtunnel/app/stdio_pipe_bridge.hpp`
- Create: `src/app/stdio_pipe_bridge.cpp`
- Modify: `CMakeLists.txt`

**Step 1: Add CLI/config support**

Support `--pipe host:port` in client mode without requiring forward listeners.

**Step 2: Add bridge wiring**

In client startup, skip `TcpListener` creation for pipe mode and instead open a single tunnel when
the server friend is online, then bridge stdio to that tunnel.

**Step 3: Run targeted tests**

Run: `./build/tests/unit_tests --gtest_filter=PipeModeTest*`

Run: `./build/tests/integration_tests --gtest_filter=PipeBridgeTest*`

Expected: PASS

### Task 9: Update Documentation

**Files:**
- Modify: `README.md`

**Step 1: Update examples**

Document:

- nested YAML format as canonical
- automatic bootstrap discovery default
- explicit override behavior
- working SSH `ProxyCommand` using `--pipe %h:%p`

**Step 2: Verify docs against CLI**

Run: `./build/toxtunnel --help`

Expected: Help text matches documented options

### Task 10: Full Verification

**Files:**
- No code changes

**Step 1: Run focused tests**

Run: `./build/tests/unit_tests`

Run: `./build/tests/integration_tests`

Expected: PASS

**Step 2: Run full suite**

Run: `cd build && ctest --output-on-failure`

Expected: 0 failures

**Step 3: Manual SSH verification**

Run a local temporary `sshd`, then:

```bash
./build/toxtunnel -c /tmp/server.yaml
./build/toxtunnel -c /tmp/client.yaml
ssh -o ProxyCommand="./build/toxtunnel -m client --server-id <ID> --pipe %h:%p" user@127.0.0.1
```

Expected: successful SSH command execution through ToxTunnel
