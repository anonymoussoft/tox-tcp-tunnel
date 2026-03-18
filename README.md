# ToxTunnel - TCP Tunnel over Tox

Modern C++ rewrite of tuntox for TCP tunneling over the Tox protocol.

## Features

- Async I/O for high performance
- Cross-platform (Linux, macOS, Windows)
- Server and client modes
- Port forwarding and pipe modes
- Authentication and access control

## Building

Requires:
- C++20 compiler (GCC 10+, Clang 13+, MSVC 2019+)
- CMake 3.20+
- toxcore library

```bash
mkdir build && cd build
cmake ..
cmake --build .
```

## Usage

See `docs/` for detailed documentation.

## License

GPLv3 (inheriting from toxcore)
