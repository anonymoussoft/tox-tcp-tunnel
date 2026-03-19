#include <gtest/gtest.h>

#include <string>

#include "toxtunnel/tox/tox_adapter.hpp"

namespace toxtunnel::tox {
namespace {

TEST(ToxAdapterTest, DispatchesQueuedFriendRequestCallbacksOnlyWhenDrained) {
    ToxAdapter adapter;

    bool called = false;
    std::string observed_message;
    adapter.set_on_friend_request(
        [&called, &observed_message](const PublicKeyArray&, std::string_view message) {
            called = true;
            observed_message = std::string(message);
        });

    PublicKeyArray public_key{};
    public_key.fill(0x42);

    adapter.enqueue_friend_request_for_test(public_key, "hello");
    EXPECT_FALSE(called);

    adapter.dispatch_pending_events_for_test();
    EXPECT_TRUE(called);
    EXPECT_EQ(observed_message, "hello");
}

}  // namespace
}  // namespace toxtunnel::tox
