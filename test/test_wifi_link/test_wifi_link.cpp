/**
 * @file test_wifi_link.cpp
 * @brief WifiLink starts a join and returns. The gap doubles from 1 s to 60 s.
 */

#include <unity.h>

#include "wifi_manager.h"

#include "../fakes/fake_clock.h"

namespace {

class FakeStation final : public gateway::WifiStation {
public:
    uint32_t connects = 0;
    bool up = false;

    bool isConnected() const override { return up; }

    void beginConnect(const char* /*ssid*/, const char* /*password*/) override { connects++; }
};

}  // namespace

void setUp(void) {}

void tearDown(void) {}

void test_empty_ssid_never_connects(void) {
    FakeClock clock;
    FakeStation station;
    gateway::WifiLink link(clock, station, "", "secret");
    link.begin();
    link.poll();
    clock.advance(gateway::kWifiBackoffMaxMs);
    link.poll();
    TEST_ASSERT_EQUAL_UINT32(0, station.connects);
    TEST_ASSERT_FALSE(link.connected());
}

void test_poll_returns_during_the_gap_and_the_gap_doubles(void) {
    FakeClock clock;
    FakeStation station;
    gateway::WifiLink link(clock, station, "lab", "secret");
    link.begin();
    TEST_ASSERT_EQUAL_UINT32(1, station.connects);

    link.poll();
    clock.advance(gateway::kWifiBackoffMinMs - 1);
    link.poll();
    TEST_ASSERT_EQUAL_UINT32(1, station.connects);

    clock.advance(1);
    link.poll();
    TEST_ASSERT_EQUAL_UINT32(2, station.connects);

    clock.advance(1999);
    link.poll();
    TEST_ASSERT_EQUAL_UINT32(2, station.connects);
    clock.advance(1);
    link.poll();
    TEST_ASSERT_EQUAL_UINT32(3, station.connects);
}

void test_connect_resets_the_gap_and_the_wait_caps_at_60s(void) {
    FakeClock clock;
    FakeStation station;
    gateway::WifiLink link(clock, station, "lab", "");
    link.begin();
    TEST_ASSERT_FALSE(link.connected());

    station.up = true;
    link.poll();
    TEST_ASSERT_TRUE(link.connected());
    TEST_ASSERT_EQUAL_UINT32(1, station.connects);

    station.up = false;
    link.poll();
    TEST_ASSERT_EQUAL_UINT32(2, station.connects);
    clock.advance(gateway::kWifiBackoffMinMs - 1);
    link.poll();
    TEST_ASSERT_EQUAL_UINT32(2, station.connects);

    uint32_t wait = gateway::kWifiBackoffMinMs;
    while (wait < gateway::kWifiBackoffMaxMs) {
        clock.advance(wait);
        link.poll();
        const uint32_t doubled = wait * 2;
        wait = doubled > gateway::kWifiBackoffMaxMs ? gateway::kWifiBackoffMaxMs : doubled;
    }
    const uint32_t capped = station.connects;
    clock.advance(gateway::kWifiBackoffMaxMs - 1);
    link.poll();
    TEST_ASSERT_EQUAL_UINT32(capped, station.connects);
    clock.advance(1);
    link.poll();
    TEST_ASSERT_EQUAL_UINT32(capped + 1, station.connects);
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    UNITY_BEGIN();
    RUN_TEST(test_empty_ssid_never_connects);
    RUN_TEST(test_poll_returns_during_the_gap_and_the_gap_doubles);
    RUN_TEST(test_connect_resets_the_gap_and_the_wait_caps_at_60s);
    return UNITY_END();
}
