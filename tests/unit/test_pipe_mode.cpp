#include <gtest/gtest.h>

#include "toxtunnel/util/config.hpp"

namespace toxtunnel {
namespace {

TEST(PipeModeTest, ParseValidPipeTarget) {
    auto result = parse_pipe_target("127.0.0.1:22");
    ASSERT_TRUE(result.has_value()) << result.error();
    EXPECT_EQ(result.value().remote_host, "127.0.0.1");
    EXPECT_EQ(result.value().remote_port, 22);
}

TEST(PipeModeTest, RejectInvalidPipeTarget) {
    auto result = parse_pipe_target("missing-port");
    EXPECT_FALSE(result.has_value());
}

TEST(PipeModeTest, ClientConfigWithPipeTargetValidatesWithoutForwards) {
    Config config = Config::default_client();
    config.client->server_id =
        "0000000000000000000000000000000000000000000000000000000000000000000000000000";
    config.client->pipe_target = PipeTarget{"127.0.0.1", 22};

    auto validation = config.validate();
    EXPECT_TRUE(validation.has_value()) << validation.error();
}

}  // namespace
}  // namespace toxtunnel
