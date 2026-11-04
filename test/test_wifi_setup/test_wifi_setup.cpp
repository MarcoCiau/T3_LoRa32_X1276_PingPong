/**
 * @file test_wifi_setup.cpp
 * @brief A posted form is stored. Cancel and the timeout keep the previous secret.
 */

#include <unity.h>

#include "wifi_manager.h"
#include "wifi_setup.h"

#include "../fakes/fake_clock.h"

#include <cstring>

namespace {

class FakeStation final : public gateway::WifiStation {
public:
    uint32_t connects = 0;
    char ssid[33] = {};
    char password[65] = {};

    bool isConnected() const override { return false; }
    void beginConnect(const char* inSsid, const char* inPassword) override {
        connects++;
        std::strncpy(ssid, inSsid != nullptr ? inSsid : "", sizeof(ssid) - 1);
        std::strncpy(password, inPassword != nullptr ? inPassword : "", sizeof(password) - 1);
    }
};

class FakeSecrets final : public gateway::WifiSecrets {
public:
    bool saved = false;
    char ssid[33] = {};
    char password[65] = {};
    char token[65] = {};

    bool load(char* outSsid,
              size_t ssidCap,
              char* outPassword,
              size_t passwordCap,
              char* outToken,
              size_t tokenCap) override {
        if (!saved || outSsid == nullptr || outPassword == nullptr || outToken == nullptr) {
            return false;
        }
        std::strncpy(outSsid, ssid, ssidCap - 1);
        std::strncpy(outPassword, password, passwordCap - 1);
        std::strncpy(outToken, token, tokenCap - 1);
        return outSsid[0] != '\0';
    }

    bool save(const char* inSsid, const char* inPassword, const char* inToken) override {
        if (inSsid == nullptr || inSsid[0] == '\0') {
            return false;
        }
        std::strncpy(ssid, inSsid, sizeof(ssid) - 1);
        std::strncpy(password, inPassword != nullptr ? inPassword : "", sizeof(password) - 1);
        if (inToken != nullptr && inToken[0] != '\0') {
            std::strncpy(token, inToken, sizeof(token) - 1);
        }
        saved = true;
        return true;
    }

    bool clear() override {
        saved = false;
        ssid[0] = '\0';
        password[0] = '\0';
        token[0] = '\0';
        return true;
    }
};

class FakePortal final : public gateway::WifiPortal {
public:
    uint32_t starts = 0;
    uint32_t stops = 0;
    bool posted = false;

    void start(const char* /*apName*/) override { starts++; }
    void stop() override { stops++; }
    void poll() override {}
    bool takePosted(char* ssid,
                    size_t ssidCap,
                    char* password,
                    size_t passwordCap,
                    char* token,
                    size_t tokenCap) override {
        if (!posted) {
            return false;
        }
        posted = false;
        std::strncpy(ssid, "lab", ssidCap - 1);
        std::strncpy(password, "secret", passwordCap - 1);
        std::strncpy(token, "device-1", tokenCap - 1);
        return true;
    }
};

}  // namespace

void setUp(void) {}

void tearDown(void) {}

void test_form_is_saved_and_cancel_keeps_the_old_secret(void) {
    FakeClock clock;
    FakeStation station;
    gateway::WifiLink wifi(clock, station, "", "");
    FakeSecrets secrets;
    FakePortal portal;
    gateway::WifiSetup setup(clock, portal, secrets, wifi, "lp-gw-ABCD");

    setup.begin();
    TEST_ASSERT_TRUE(setup.active());
    TEST_ASSERT_EQUAL_UINT32(1, portal.starts);

    portal.posted = true;
    setup.poll();
    TEST_ASSERT_FALSE(setup.active());
    TEST_ASSERT_TRUE(setup.takeSaved());
    TEST_ASSERT_FALSE(setup.takeSaved());
    TEST_ASSERT_TRUE(secrets.saved);
    TEST_ASSERT_EQUAL_STRING("lab", secrets.ssid);
    TEST_ASSERT_EQUAL_STRING("secret", secrets.password);
    TEST_ASSERT_EQUAL_STRING("device-1", secrets.token);
    TEST_ASSERT_TRUE(secrets.save("lab", "secret", ""));
    TEST_ASSERT_EQUAL_STRING("device-1", secrets.token);
    TEST_ASSERT_EQUAL_UINT32(1, portal.stops);
    TEST_ASSERT_EQUAL_UINT32(1, station.connects);
    TEST_ASSERT_EQUAL_STRING("lab", station.ssid);
    TEST_ASSERT_EQUAL_STRING("secret", station.password);

    setup.begin();
    setup.cancel();
    TEST_ASSERT_FALSE(setup.active());
    TEST_ASSERT_EQUAL_STRING("lab", secrets.ssid);
    TEST_ASSERT_EQUAL_UINT32(2, portal.stops);

    setup.begin();
    clock.advance(gateway::WifiSetup::kTimeoutMs);
    setup.poll();
    TEST_ASSERT_FALSE(setup.active());
    TEST_ASSERT_EQUAL_STRING("lab", secrets.ssid);
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    UNITY_BEGIN();
    RUN_TEST(test_form_is_saved_and_cancel_keeps_the_old_secret);
    return UNITY_END();
}
