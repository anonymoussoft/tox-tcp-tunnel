# LAN Bootstrap Mode Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Implement a shared top-level `tox` configuration block and `bootstrap_mode: lan` so client and server can bootstrap within a LAN without depending on `https://nodes.tox.chat/json`, while keeping manual bootstrap nodes as optional supplements.

**Architecture:** Move shared Tox network settings into `Config::tox`, normalize legacy config into that structure, and thread the resulting settings through `TunnelServer`, `TunnelClient`, `ToxAdapter`, and `BootstrapSource`. Keep `auto` mode behavior unchanged, add explicit LAN mode semantics in bootstrap resolution, and verify the new config model and bootstrap behavior with unit and integration tests first.

**Tech Stack:** C++20, yaml-cpp, Google Test, CMake, c-toxcore

---

### Task 1: Add shared tox config coverage

**Files:**
- Modify: `tests/unit/test_config.cpp`
- Modify: `tests/integration/test_config_pipeline.cpp`

**Step 1: Write the failing tests**

Add tests that expect:
- canonical `tox:` server config to parse into shared Tox fields,
- canonical `tox:` client config to parse into shared Tox fields,
- legacy server bootstrap fields to normalize into shared Tox fields,
- `bootstrap_mode: lan` with `udp_enabled: false` to fail validation,
- serialized YAML to contain canonical `tox:` output instead of legacy bootstrap fields.

**Step 2: Run tests to verify they fail**

Run: `cmake --build build --target unit_tests integration_tests -j4 && ./build/tests/unit_tests --gtest_filter=ConfigTest* && ./build/tests/integration_tests --gtest_filter=ConfigPipelineTest*`

Expected: FAIL because shared `tox` config and `bootstrap_mode` are not implemented yet.

**Step 3: Write minimal implementation**

Do not implement full application logic yet. Only add enough production code to parse, validate,
normalize, and serialize the shared `tox` config so the new config tests can pass.

**Step 4: Run tests to verify they pass**

Run the same commands from Step 2.

Expected: PASS for the new config-focused tests.

### Task 2: Add bootstrap-mode behavior coverage

**Files:**
- Modify: `tests/unit/test_bootstrap_source.cpp`
- Modify: `include/toxtunnel/tox/bootstrap_source.hpp`
- Modify: `src/tox/bootstrap_source.cpp`

**Step 1: Write the failing tests**

Add tests that expect:
- `auto` mode still fetches or falls back to cache,
- `lan` mode skips remote fetch,
- `lan` mode ignores cache fallback,
- `lan` mode returns explicit configured nodes unchanged.

**Step 2: Run tests to verify they fail**

Run: `cmake --build build --target unit_tests -j4 && ./build/tests/unit_tests --gtest_filter=BootstrapSourceTest*`

Expected: FAIL because bootstrap resolution is not mode-aware yet.

**Step 3: Write minimal implementation**

Teach `BootstrapSource::resolve_bootstrap_nodes(...)` to accept bootstrap mode and apply the
documented `auto` versus `lan` resolution policy.

**Step 4: Run tests to verify they pass**

Run the same commands from Step 2.

Expected: PASS for all bootstrap source tests.

### Task 3: Thread shared tox config through runtime

**Files:**
- Modify: `include/toxtunnel/tox/tox_adapter.hpp`
- Modify: `src/tox/tox_adapter.cpp`
- Modify: `src/app/tunnel_server.cpp`
- Modify: `src/app/tunnel_client.cpp`

**Step 1: Write the failing test**

Add or extend a focused unit test that proves LAN mode configuration can be stored in
`ToxAdapterConfig` and that runtime bootstrap logic does not treat an empty node list in LAN mode
as a discovery failure.

**Step 2: Run test to verify it fails**

Run: `cmake --build build --target unit_tests -j4 && ./build/tests/unit_tests --gtest_filter=ToxAdapterTest*`

Expected: FAIL because adapter config and runtime bootstrap semantics do not yet support shared
bootstrap mode.

**Step 3: Write minimal implementation**

Update runtime code to:
- derive ToxAdapter settings from `Config::tox`,
- set explicit local discovery behavior,
- use `BootstrapSource` with the selected mode,
- and keep manual bootstrap nodes supplemental in LAN mode.

**Step 4: Run test to verify it passes**

Run the same commands from Step 2.

Expected: PASS for adapter-focused tests.

### Task 4: Update docs and canonical examples

**Files:**
- Modify: `docs/CONFIGURATION.md`
- Modify: `docs/ARCHITECTURE.md`
- Modify: `README.md`

**Step 1: Write the doc changes**

Update docs to present the top-level `tox:` block as canonical, explain `bootstrap_mode: auto`
and `bootstrap_mode: lan`, and describe LAN-mode limitations and compatibility behavior.

**Step 2: Review for consistency**

Manually verify docs match the implemented config shape and do not advertise stale legacy-only
examples as canonical.

### Task 5: Full verification

**Files:**
- Modify: `.gitignore` (if we decide to keep project-local worktree ignores in the branch)

**Step 1: Run targeted verification**

Run:
- `cmake --build build --target unit_tests integration_tests -j4`
- `./build/tests/unit_tests`
- `./build/tests/integration_tests`

Expected: PASS.

**Step 2: Run repo-level verification**

Run:
- `cd build && ctest --output-on-failure`

Expected: PASS.

**Step 3: Inspect diff**

Run: `git status --short` and `git diff --stat`

Expected: only intended implementation changes remain.
