#pragma once

#include <memory>


//////////////////////////////////////////////////////
//  Stop Token
//////////////////////////////////////////////////////
class StopToken;
using StopTokenPtr = std::shared_ptr<StopToken>;


/**
 * @brief Creates a shared stop token in its not-running state.
 * @return Pointer to the newly allocated token.
 */
StopTokenPtr
CreateStopToken();


/**
 * @brief Marks the token as running by acquiring its state mutex.
 * @param stopper Token to start; must be non-null.
 */
void
Start(StopTokenPtr stopper);


/**
 * @brief Requests that a running token stop.
 * @param stopper Token to stop; must be non-null and started by this thread.
 */
void
RequestStop(StopTokenPtr stopper);


/**
 * @brief Checks without blocking whether stop was requested.
 * @param stopper Token to inspect; must be non-null.
 * @return true if stop was requested; otherwise false.
 */
bool
StopRequested(StopTokenPtr stopper);


