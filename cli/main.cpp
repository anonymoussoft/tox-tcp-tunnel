#include <CLI/CLI.hpp>
#include <spdlog/spdlog.h>

int main(int argc, char* argv[]) {
    CLI::App app{"ToxTunnel - TCP Tunnel over Tox"};

    std::string mode;
    app.add_option("-m,--mode", mode, "Operating mode: server or client");

    CLI11_PARSE(app, argc, argv);

    spdlog::info("ToxTunnel starting...");

    return 0;
}
