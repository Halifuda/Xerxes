#ifndef __ERROR_RETRYSTATISTICS__
#define __ERROR_RETRYSTATISTICS__
#include <cinttypes>
#include <vector>
#include <map>
#include "util/old/SimpleSSD_types.h"
#include "retryPage.hh"
#include "sim/trace.hh"
#include "config.hh"
#include "sim/config_reader.hh"

namespace SimpleSSD {

    namespace ERROR {
        class MyCompare {
            public:
	            bool operator()(::CPDPBP address1,::CPDPBP address2) const
	            {
		           bool inc=false;
                   if(address1.Channel<address2.Channel){
                        inc=true;
                   }else if(address1.Channel==address2.Channel){
                        if(address1.Package<address2.Package){
                            inc=true;
                        }else if(address1.Package==address2.Package){
                            if(address1.Die<address2.Die){
                                inc=true;
                            }else if(address1.Die==address2.Die){
                                if(address1.Plane<address2.Plane){
                                    inc=true;
                                }else if(address1.Plane==address2.Plane){
                                    if(address1.Block<address2.Block){
                                        inc=true;
                                    }else if(address1.Block==address2.Block){
                                        if(address1.Page<address2.Page){
                                            inc=true;
                                        }
                                    }
                                }
                            }
                        }
                   }
                   return inc;
	            }
        };

        class RetryStatistics {
           
            public:
                RetryStatistics(ConfigReader &c);
                ~RetryStatistics();
                bool isSameAddress(::CPDPBP address1,::CPDPBP address2,uint32_t type);
                void addRetry(::CPDPBP address,uint64_t tick);
                void closeRetry(::CPDPBP,uint64_t);
                void closeRetry(::CPDPBP,uint64_t,uint64_t,bool);
                void showStat();
                void showStatToFile();
                void showRetryRequest();
                void showRetryDistribution();
                void showRetryLatency();
                void printCPDPBP(::CPDPBP addr,uint32_t count);
                void writerToFile();
                void setRetryModule(bool retry);
                bool getRetryModule();
                void setRequestNumber(uint32_t number);
	      	void setRetryRequestNumber();
             private:
                std::vector<RetryPage> retryRequests;
                uint32_t requestsNumber;
	      	uint32_t retryRequestsNumber;
                bool retryModule;
                ConfigReader &conf;
                Config config = conf.errorConfig;


        };

    }  // namespace FTL

}  // namespace SimpleSSD

#endif