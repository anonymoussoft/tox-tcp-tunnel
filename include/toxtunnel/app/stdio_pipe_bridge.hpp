#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <span>
#include <string>
#include <thread>

#include "toxtunnel/util/expected.hpp"

namespace toxtunnel::app {

class StdioPipeBridge {
   public:
    using InputCallback = std::function<void(std::span<const uint8_t> data)>;
    using ClosedCallback = std::function<void()>;

    StdioPipeBridge(int input_fd, int output_fd);
    ~StdioPipeBridge();

    StdioPipeBridge(const StdioPipeBridge&) = delete;
    StdioPipeBridge& operator=(const StdioPipeBridge&) = delete;
    StdioPipeBridge(StdioPipeBridge&&) = delete;
    StdioPipeBridge& operator=(StdioPipeBridge&&) = delete;

    [[nodiscard]] util::Expected<void, std::string> start(InputCallback on_input,
                                                          ClosedCallback on_closed);

    void write_output(std::span<const uint8_t> data);

    void stop();

   private:
    void read_loop();
    void close_descriptors();

    int input_fd_ = -1;
    int output_fd_ = -1;
    std::atomic<bool> running_{false};
    std::thread input_thread_;
    std::mutex output_mutex_;
    InputCallback on_input_;
    ClosedCallback on_closed_;
};

}  // namespace toxtunnel::app
