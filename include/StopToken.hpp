#pragma once

#include <memory>


//////////////////////////////////////////////////////
//  Stop Token
//////////////////////////////////////////////////////
class StopToken;
using StopTokenPtr = std::shared_ptr<StopToken>;


/* Create a new StopToken Pointer.
 *
 * @result StopTokenPtr - Pointer to a new StopToken object.
 */
StopTokenPtr
CreateStopToken();


/* Put the stopper in a running state.
 *
 * @param stopper - Pointer to a StopToken object;
 */
void
Start(StopTokenPtr);


/* Request an asynchronous stop.
 *
 * @param stopper - Pointer to a StopToken object;
 */
void
RequestStop(StopTokenPtr);


/* Check if stop was requested.
 *
 * @param stopper - Pointer to a StopToken object;
 */
bool
StopRequested(StopTokenPtr);


