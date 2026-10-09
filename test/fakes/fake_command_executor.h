/**
 * @file fake_command_executor.h
 * @brief CommandExecutorPort that records calls and reports success.
 */

#pragma once

#include <command_executor_port.h>

#include <cstdint>

/**
 * @brief Counts execute() calls for host tests.
 *
 * The check-in must call this once per new command id and not at all for a repeat.
 */
class FakeCommandExecutor final : public CommandExecutorPort {
public:
    uint32_t calls = 0;
    uint32_t lastId = 0;
    uint8_t lastType = 0;

    /**
     * @brief Record the call and return Ok.
     * @return CommandResultStatus::Ok.
     */
    proto::CommandResultStatus execute(uint32_t commandId,
                                       uint8_t commandType,
                                       const uint8_t* /*body*/,
                                       uint8_t /*bodyLen*/) override {
        calls++;
        lastId = commandId;
        lastType = commandType;
        return proto::CommandResultStatus::Ok;
    }
};
