# ToxTunnel

TCP tunneling over the [Tox](https://tox.chat) P2P network.

Forward any TCP port through Tox with end-to-end encryption, no central server, and automatic NAT traversal.

---

## Why ToxTunnel?

### vs. Specific Solutions

| | ToxTunnel | frp / ngrok | Tailscale / ZeroTier | WireGuard / OpenVPN |
|---|---|---|---|---|
| **Need Registration?** | **No** | ngrok requires account; frp needs relay server | Account required | No |
| **Need Central Server?** | **No** | Yes (relay/broker) | Coordination server | Manual or self-hosted |
| **Cost** | **Free, forever** | Free tiers limited; paid plans | Free tier limited | Free (self-hosted) |
| **Setup Steps** | Build → run | Deploy relay + configure client | Sign up → install → authorize | Generate keys → configure routes |
| **NAT Traversal** | **Automatic** (Tox DHT) | Relay handles it | DERP relay fallback | Manual port forward or STUN |
| **LAN Optimization** | **Direct P2P, zero hop** | Still routes through relay | Can do direct, but needs coordinator | Direct if configured |
| **E2E Encryption** | **Always on** (NaCl) | TLS to relay; relay can inspect | WireGuard-based | Yes |
| **Privacy** | **No accounts, no logs, no metadata** | Provider sees traffic metadata | Provider sees connection graph | Self-hosted = private |
| **Bandwidth** | **Your ISP only** | Relay server bottleneck | DERP relay bottleneck | Your ISP / server |
| **Works Offline / Air-gapped** | **Yes** (LAN mode) | No | No | Yes (manual config) |

### Key Advantages

- **No Registration Required** - Just install and run. No account, no email, no phone number, no verification. Generate a Tox ID locally and start tunneling immediately. Compare this to ngrok (account + authtoken), Tailscale (Google/Microsoft/GitHub login), or ZeroTier (account + network ID).

- **True P2P, No Relay** - Data flows directly between peers. No relay server to become a bottleneck, a single point of failure, or a surveillance point. frp and ngrok route everything through a central server; even Tailscale falls back to DERP relays when direct connection fails.

- **LAN-First Design** - When both devices are on the same local network, ToxTunnel connects directly via LAN with zero hops and near-zero latency — no internet required, no external coordination. Most VPN and tunnel solutions still route through external servers even for same-network peers.

- **Zero Config NAT Traversal** - The Tox DHT network handles NAT hole-punching automatically. No router port forwarding, no STUN/TURN setup, no firewall rules. It just works.

- **End-to-End Encrypted** - All traffic is encrypted with NaCl (libsodium) — the same cryptography used by Signal. Encryption is mandatory and cannot be disabled. Unlike TLS-to-relay solutions, no intermediary can inspect your data.

- **No Metadata Leakage** - No accounts means no identity graph. No central server means no connection logs. Your traffic patterns are visible only to your ISP, not to any service provider.

- **Cross-Platform** - Works on Linux, macOS, Windows with consistent experience.

- **Open Source** - Full transparency, auditable code, GPLv3 licensed.

---

## How It Works

### Principle Overview

```
                          No registration, no server, no config
                          ──────────────────────────────────────

    Your Laptop                                            Remote Machine
  ┌──────────────┐          Tox P2P Network            ┌──────────────────┐
  │              │     ┌───────────────────────┐       │                  │
  │  App ──► :2222 ════╡  Encrypted P2P Link  ╞══════► :22 ──► SSH       │
  │              │     │  (NaCl / libsodium)   │       │                  │
  │  ToxTunnel   │     └───────────────────────┘       │  ToxTunnel       │
  │   Client     │       NAT traversal: auto           │   Server         │
  └──────────────┘       LAN: direct, 0 hop            └──────────────────┘
```

**Three steps, no accounts:**

```
Server:   ./toxtunnel -m server           → prints Tox ID
Client:   ./toxtunnel -m client --server-id <TOX_ID>
Connect:  ssh -p 2222 localhost           → done
```

### How Connection Is Established

```
  Client                     Tox DHT                    Server
    │                           │                          │
    │   1. Generate Tox ID      │    1. Generate Tox ID    │
    │       (local, instant)    │       (local, instant)   │
    │                           │                          │
    │   2. Lookup Server ID ──► │                          │
    │                           │ ◄── 2. Announce to DHT   │
    │   3. NAT hole-punch ──────┼──────────────────────►   │
    │                           │                          │
    │   4. ◄══════ Encrypted P2P channel established ═══►  │
    │                           │                          │
    │   5. TCP traffic flows directly, no relay             │
    │      ◄══════════════════════════════════════════►     │
```

### LAN vs Internet

```
  Same LAN (direct, fastest):

    Client ◄══════════════════════► Server
              Direct P2P, 0 hops
              No internet needed


  Different Networks (NAT traversal):

    Client ◄───► NAT ◄───► Tox DHT ◄───► NAT ◄───► Server
                       hole-punch
                 then: Client ◄════════════► Server
                         Direct P2P, no relay
```

---

## Quick Start

### 1. Build

```bash
# macOS
brew install cmake pkg-config libsodium

# Ubuntu/Debian
sudo apt install -y build-essential cmake git pkg-config libsodium-dev

# Clone and build
git clone --recursive https://github.com/anonymoussoft/tox-tcp-tunnel.git
cd tox-tcp-tunnel
cmake -B build && cmake --build build
```

For Windows, Docker, etc., see [docs/BUILDING.md](docs/BUILDING.md).

## SSH Over ToxTunnel

### Scenario

You have two machines:
- **Server Machine** (remote): The machine you want to SSH into
- **Client Machine** (local): Your laptop/desktop

Both machines need ToxTunnel built and `ssh` installed.

---

### Step 1: Start Server (on remote machine)

```bash
./build/toxtunnel -m server
```

Output:
```
Server Tox address: DE47F247CE6D7BE29A5903A234A045A227C6CB969943A8317EA74F7D38810D10D43C53082F2B
TunnelServer started
```

**Copy this 76-character Tox address.** You'll need it on the client.

> Note: The server must stay running. Run it in a terminal or use `nohup`, `tmux`, or systemd.

---

### Step 2: Start Client (on local machine)

Create a config file `client.yaml`:

```yaml
mode: client
data_dir: ~/.toxtunnel

client:
  # Paste the server's Tox address here
  server_id: "DE47F247CE6D7BE29A5903A234A045A227C6CB969943A8317EA74F7D38810D10D43C53082F2B"

  forwards:
    - local_port: 2222      # Local port to listen on
      remote_host: 127.0.0.1  # SSH server on remote machine
      remote_port: 22        # SSH port
```

Start the client:

```bash
./build/toxtunnel -c client.yaml
```

Output:
```
Listening on local port 2222 -> 127.0.0.1:22
Client started
```

> Wait for "Server friend 0 is now online" before connecting (may take 10-30 seconds).

---

### Step 3: Connect via SSH

In a new terminal on your local machine:

```bash
ssh -p 2222 your_username@localhost
```

That's it! Your SSH connection is now routed through the Tox network.

---

## Alternative: SSH ProxyCommand (No Config File)

If you prefer not to create a config file, you can use SSH's ProxyCommand feature.

### How It Works

The `--pipe` mode connects stdin/stdout directly to the tunnel, allowing SSH to use it as a transport.

### Example Command

```bash
# Replace with your server's Tox address
SERVER_ID="DE47F247CE6D7BE29A5903A234A045A227C6CB969943A8317EA74F7D38810D10D43C53082F2B"

ssh -o ProxyCommand="./build/toxtunnel -m client --server-id ${SERVER_ID} --pipe 127.0.0.1:22" user@dummy
```

**Explanation:**
- `--pipe 127.0.0.1:22` - Connect to port 22 on the server machine
- `user@dummy` - The hostname is ignored; only the username matters
- Each SSH session starts a new ToxTunnel instance

### SSH Config Setup

Add to `~/.ssh/config`:

```
Host tox-remote
    User your_username
    ProxyCommand /path/to/toxtunnel -m client --server-id YOUR_SERVER_TOX_ADDRESS --pipe 127.0.0.1:22
```

Then simply:

```bash
ssh tox-remote
```

---

## Testing on a Single Machine

To test locally before deploying:

```bash
# Terminal 1: Start server
./build/toxtunnel -m server

# Terminal 2: Start client (use the server's Tox address from Terminal 1)
./build/toxtunnel -m client --server-id <SERVER_TOX_ADDRESS> -d /tmp/tox-client

# Wait for connection, then:
# Terminal 3: Test SSH
ssh -p 2222 localhost
```

---

## CLI Reference

```
toxtunnel [OPTIONS]
```

| Flag                    | Description                                  |
| ----------------------- | -------------------------------------------- |
| `-c, --config FILE`     | YAML config file                             |
| `-m, --mode MODE`       | `server` or `client`                         |
| `-d, --data-dir DIR`    | Directory for Tox identity (default: `.`)    |
| `-l, --log-level LEVEL` | `trace`, `debug`, `info`, `warn`, `error`    |
| `-p, --port PORT`       | TCP port for Tox (server mode, default: 33445) |
| `--server-id ID`        | Server's 76-char Tox address (client mode)   |
| `--pipe HOST:PORT`      | Pipe mode: connect stdin/stdout to tunnel    |
| `-v, --version`         | Show version                                 |

---

## Multiple Port Forwards

Forward multiple services through one client:

```yaml
client:
  server_id: "SERVER_TOX_ADDRESS"
  forwards:
    - local_port: 2222
      remote_host: 127.0.0.1
      remote_port: 22        # SSH

    - local_port: 5432
      remote_host: 127.0.0.1
      remote_port: 5432      # PostgreSQL

    - local_port: 8080
      remote_host: 192.168.1.100
      remote_port: 80        # Web server on remote LAN

    - local_port: 3389
      remote_host: 127.0.0.1
      remote_port: 3389      # Windows RDP

    - local_port: 873
      remote_host: 127.0.0.1
      remote_port: 873        # Rsync
```

### More Scenarios

For detailed guides on various use cases, see:
- [HTTP Tunneling](docs/HTTP_TUNNELING.md) - Web server and proxy access
- [Database Tunneling](docs/DATABASE_TUNNELING.md) - Secure database access
- [Advanced Scenarios](docs/ADVANCED_SCENARIOS.md) - RDP/VNC, Rsync, NAS, custom services

---

## Documentation

- [Building Guide](docs/BUILDING.md) - Windows, Docker, detailed instructions
- [Configuration](docs/CONFIGURATION.md) - Access control, bootstrap nodes, advanced options
- [Architecture](docs/ARCHITECTURE.md) - Protocol, threading model, internals
- [HTTP Tunneling](docs/HTTP_TUNNELING.md) - Web server access, HTTP proxy scenarios
- [Database Tunneling](docs/DATABASE_TUNNELING.md) - Database access (PostgreSQL, MySQL, Redis, etc.)
- [Advanced Scenarios](docs/ADVANCED_SCENARIOS.md) - RDP/VNC, Rsync, NAS, custom services

---

## License

GPLv3 - see [LICENSE](LICENSE)
