/**
 * @file command_executor_port.h
 * @brief Side effects for a command carried inside an Ack.
 */

#pragma once

#include "../protocol/message_types.h"

#include <cstdint>

/** @brief command_type for the no-op executor. No other type is executed in this version. */
constexpr uint8_t kNopCommandType = 0;

/**
 * @brief Runs one accepted command.
 *
 * The check-in calls this only for kNopCommandType, and only when the command id
 * has not already been completed. Implementations must not transmit.
 */
class CommandExecutorPort {
public:
    virtual ~CommandExecutorPort() = default;

    /**
     * @brief Perform @p commandType once.
     * @param commandId Command id from the Ack. Not executed twice for the same id.
     * @param commandType Application command type.
     * @param body Command body. May be null when @p bodyLen is 0.
     * @param bodyLen Body length.
     * @return Status stored in the CommandResult. Ok for the no-op type.
     */
    virtual proto::CommandResultStatus execute(uint32_t commandId,
                                               uint8_t commandType,
                                               const uint8_t* body,
                                               uint8_t bodyLen) = 0;
};
