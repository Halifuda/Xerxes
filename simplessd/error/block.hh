#pragma once

#ifndef __ERROR_BLOCK__
#define __ERROR_BLCOK__

#include "config.hh"
#include "sim/config_reader.hh"
#include "util/old/SimpleSSD_types.h"
#include "pal/old/PALStatistics.h"
#include "pal/pal.hh"
#include "pal/config.hh"
#include "count/Counts.hh"


namespace SimpleSSD{
namespace ERROR{

    class Blockstate{
        private:
            int *block;
           
        public:
            void update_blockstate(::Command cmd, ::CPDPBP addr, PAL::Parameter param);
             ~Blockstate();
            void setblock(uint32_t blocksize);
            int get_state(uint32_t blockID);

    };

    void InitBlockState(ConfigReader conf);

    void deInitBlockState();

    void update_blockstate(::Command cmd, ::CPDPBP addr, PAL::Parameter param);

    bool IsOpenBlock(uint32_t blockID);

    }
}

#endif