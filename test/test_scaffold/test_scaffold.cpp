#include <unity.h>

#include <clock_port.h>
#include <command_executor_port.h>
#include <message_types.h>
#include <packet_codec.h>
#include <radio_port.h>
#include <random_port.h>
#include <record_store_port.h>
#include <security.h>
#include <sequence_manager.h>

#include "../fakes/fakes.h"
#include "../../src/gateway/command_queue.h"
#include "../../src/gateway/mqtt_service.h"
#include "../../src/gateway/node_registry.h"
#include "../../src/gateway/radio_service.h"
#include "../../src/gateway/storage.h"
#include "../../src/gateway/wifi_manager.h"
#include "../../src/node/command_service.h"
#include "../../src/node/provisioning.h"
#include "../../src/node/radio_service.h"
#include "../../src/node/telemetry_service.h"

void setUp(void) {}

void tearDown(void) {}

void test_native_env_runs(void) {
    TEST_ASSERT_TRUE(true);
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    UNITY_BEGIN();
    RUN_TEST(test_native_env_runs);
    return UNITY_END();
}
