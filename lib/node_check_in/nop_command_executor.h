/**
 * @file nop_command_executor.h
 * @brief Production executor for command_type 0. It performs no side effect.
 */

#pragma once

#include <command_executor_port.h>

/**
 * @brief Accepts kNopCommandType and reports success.
 *
 * Any other type returns Failed. The check-in does not call execute for those types.
 */
class NopCommandExecutor final : public CommandExecutorPort {
public:
    /**
     * @brief Report Ok for the no-op command type.
     * @return CommandResultStatus::Ok when @p commandType is kNopCommandType, otherwise Failed.
     */
    proto::CommandResultStatus execute(uint32_t /*commandId*/,
                                       uint8_t commandType,
                                       const uint8_t* /*body*/,
                                       uint8_t /*bodyLen*/) override {
        if (commandType != kNopCommandType) {
            return proto::CommandResultStatus::Failed;
        }
        return proto::CommandResultStatus::Ok;
    }
};
