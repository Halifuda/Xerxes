#ifndef __LDPC_H__
#define __LDPC_H__

#include <map>
#include <istream>
#include <streambuf>
#include <fstream>
#include <sstream>
#include <vector>
#include <string>
#include <stdlib.h>
#include <cmath>
#include <iostream>


namespace SimpleSSD{
namespace ERROR{
    typedef struct{
        uint32_t bit;
        double UBER;
        double latency;
    } ldpcType;

    class LDPC{
        public:
            LDPC();
            virtual ~LDPC() {}
            virtual void read_csv();
            virtual double get_UBER(uint32_t b);
            virtual uint64_t get_latency(uint32_t b);
        private:
            std::vector<ldpcType> ldpcs;


    };


} // namespace error
}

#endif