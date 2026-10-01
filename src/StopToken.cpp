#include "StopToken.hpp"


/**
 * @file StopToken.cpp
 * @brief Implements the shared stop-token helpers.
 *
 * A locked mutex represents the running state; an unlocked mutex represents a
 * requested stop. This implementation relies on Start and Stop being called
 * by the same thread because std::mutex must be unlocked by its owning thread.
 */

/**
 * @brief Stores the stop state as ownership of a mutex.
 *
 * The mutex begins unlocked. Start acquires it, RequestStop releases it, and
 * StopRequested reports whether a non-blocking acquisition succeeds.
 */
class StopToken {
  std::mutex m_Done;
public:

  /**
   * @brief Allocates a token in the not-running state.
   * @return A shared pointer to the newly allocated token.
   */
  static StopTokenPtr
  Create () {
    return std::make_shared<StopToken>();
  }

  /**
   * @brief Marks the token as running by locking its mutex.
   *
   * This call blocks if the mutex is already locked. The calling thread must
   * later call Stop to release the mutex.
   */
  void Start() { m_Done.lock();   }

  /**
   * @brief Releases the running-state mutex to request a stop.
   *
   * The mutex must be owned by the calling thread; unlocking it from another
   * thread is undefined behavior.
   */
  void Stop()  { m_Done.unlock(); }

  /**
   * @brief Checks whether the token's mutex is currently unlocked.
   *
   * If the mutex can be acquired immediately, it is immediately released and
   * the method returns true. Otherwise it returns false without blocking.
   *
   * @return true when the mutex was unlocked at the time of the check.
   */
  bool
  StopRequested() {
    if (m_Done.try_lock()) {
      Stop();
      return true;
    }
    return false;
  }
};


/**
 * @brief Creates a shared stop token.
 * @return A pointer to a newly created token in the not-running state.
 */
StopTokenPtr
CreateStopToken() {
  return StopToken::Create();
}


/**
 * @brief Marks a token as running.
 *
 * The token pointer must be non-null. This call locks the token's mutex and
 * blocks if it is already locked.
 *
 * @param stopper Shared pointer to the token to start.
 */
void
Start(
  StopTokenPtr stopper
) {
  stopper->Start();
}


/**
 * @brief Requests that a token stop running.
 *
 * The token pointer must be non-null, and the calling thread must own the
 * mutex acquired by Start.
 *
 * @param stopper Shared pointer to the token to stop.
 */
void
RequestStop(
  StopTokenPtr stopper
) {
  stopper->Stop();
}


/**
 * @brief Checks whether a stop has been requested.
 *
 * This is a non-blocking check. The token pointer must be non-null.
 *
 * @param stopper Shared pointer to the token to check.
 * @return true if the token mutex could be acquired, indicating the unlocked
 *         state; otherwise false.
 */
bool
StopRequested(
  StopTokenPtr stopper
) {
  return stopper->StopRequested();
}


