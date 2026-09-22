#include "StopToken.hpp"


class StopToken {
  std::mutex m_Done;
public:

  static StopTokenPtr
  Create () {
    return std::make_shared<StopToken>();
  }

  void Start() { m_Done.lock();   }
  void Stop()  { m_Done.unlock(); }

  bool
  StopRequested() {
    if (m_Done.try_lock()) {
      Stop();
      return true;
    }
    return false;
  }
};


StopTokenPtr
CreateStopToken() {
  return StopToken::Create();
}


void
Start(
  StopTokenPtr stopper
) {
  stopper->Start();
}


void
RequestStop(
  StopTokenPtr stopper
) {
  stopper->Stop();
}


bool
StopRequested(
  StopTokenPtr stopper
) {
  return stopper->StopRequested();
}


