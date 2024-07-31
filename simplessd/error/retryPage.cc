#include "error/retryPage.hh"

namespace SimpleSSD{
    namespace ERROR{
		RetryPage::RetryPage(::CPDPBP address, uint64_t tick)
    	: addr(address), retryCurrentBeginTime(tick) {
            retryCount=0;
            retryCompleted=false;
  			readState = true;
            retryCurrentBeginTime=tick;
        }
        RetryPage::~RetryPage(){
            perRetryTime.clear();
        }
    
        void RetryPage::addRetry(uint64_t tick){
            uint64_t time=tick-retryCurrentBeginTime;
            perRetryTime.push_back(time);
            retryCurrentBeginTime=tick;
            retryCount++;
        }

        void RetryPage::setRetryCompleted(){
            retryCompleted=true;
        }

        bool RetryPage::getRetryCompleted(){
            return retryCompleted;
	}
	void RetryPage::setReadState(bool readSuccess) {
	    readState=readSuccess;
	}
	bool RetryPage::getReadState() {
	    return readState;
	}

        uint32_t RetryPage::getRetryCount(){
            return retryCount;
        }

        uint64_t RetryPage::getRetryTime(){
            uint64_t total=0;
            for(uint32_t i=0;i<retryCount;i++){
                total+=perRetryTime[i];
            }
            return total;
        }

        ::CPDPBP RetryPage::getAddress(){
            return addr;
        }
       
    }
}