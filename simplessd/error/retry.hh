#ifndef __ERROR_RETRY__
#define __ERROR_RETRY__

#include "config.hh"
#include "LDPC.hh"
#include "util/old/SimpleSSD_types.h"
#include "pal/old/PALStatistics.h"
#include "pal/old/PAL2.h"
#include "retryStatistics.hh"
#include "sim/config_reader.hh"
#include "count/OpenBlock.hh"
#include "count/Counts.hh"
#include "sim/trace.hh"
#include "block.hh"

namespace SimpleSSD{

namespace ERROR{

    void write_csv(std::string str);

    class Retry{
        public:
            Retry(ConfigReader &c): conf(c) {};
            ~Retry(){
                std::cout<<count_close<<" "<<count_open<<std::endl;
            }
            virtual bool is_Retry(CPDPBP addr, uint32_t retry, bool isopen, uint32_t retrntion);
            // virtual bool adjust_voltage(::PALasync *palasync, ::Command &cmd, CPDPBP addr, uint32_t retention, SimpleSSD::PAL::Request *pReq,
            //           uint64_t *pCounter);
            virtual void submit(){return;}
            virtual uint32_t get_ERROR(uint32_t blockID ,uint32_t pageID, uint32_t retry, bool isopen, uint32_t retention);
            virtual uint32_t get_retention(uint32_t BlockID, uint64_t reqID);
            ConfigReader &conf;
            LDPC ldpc;
            Config config = conf.errorConfig;
            RetryStatistics retryStatistics = conf;
            uint32_t count_close = 0;
            uint32_t count_open = 0;
            int NAND_type = conf.readInt(CONFIG_PAL, 10);
    };
    }

}

#endif