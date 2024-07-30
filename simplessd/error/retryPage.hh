#ifndef __ERROR_RETRYPAGE__
#define __ERROR_RETRYPAGE__
#include <cinttypes>
#include <vector>
#include <map>
#include "util/old/SimpleSSD_types.h"
namespace SimpleSSD {

namespace ERROR {

class RetryPage {
 private:
  ::CPDPBP addr;
  uint32_t retryCount;
  bool retryCompleted;
  bool readState;
  uint64_t retryCurrentBeginTime;
  std::vector<uint64_t> perRetryTime;


 public:
  RetryPage(::CPDPBP,uint64_t);
  ~RetryPage();

  void addRetry(uint64_t tick);
  void setRetryCompleted();
  bool getRetryCompleted();
  void setReadState(bool);
  bool getReadState();
  uint32_t getRetryCount();
  uint64_t getRetryTime();
  ::CPDPBP getAddress();


};

}  // namespace FTL

}  // namespace SimpleSSD

#endif