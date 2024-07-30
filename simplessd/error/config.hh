#ifndef __ERROR_CONFIG__
#define __ERROR_CONFIG__

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
#include "sim/base_config.hh"
#include "pal/config.hh"
#include <random>
#include<chrono>

#define LSB 0
#define CSB 1
#define MSB 2

#define R1 0
#define R2 1
#define R3 2
#define R4 3
#define R5 4
#define R6 5
#define R7 6

namespace SimpleSSD{

namespace ERROR{

 typedef std::map<int, float> difference;
 typedef int voltage;
//  typedef std::vector<int> DETABLE;
 typedef std::map<int, std::map<int, int>> SHIFTTABLE;
 typedef int PageType;
 typedef std::map<int, std::vector<int>> RETRYTABLE;
 typedef std::map<uint32_t, std::map<uint8_t, double>> ERRORTABLE;
int _16_10_(unsigned char buma);
int hex_to_10(std::string str);

 class Config{
    public:
        Config();
        virtual void read_csv(std::string file_name, difference &table);
        virtual double read_csv(uint32_t PE, uint32_t DR, uint32_t blockID, uint32_t pageID, uint32_t retry, double &ratio, double &mean_error, bool isopen);
        virtual RETRYTABLE read_csv(std::string dir);
        virtual void read_csv();
        virtual void read_csv(uint8_t mode);
        virtual uint32_t get_layerID(uint32_t pageID);
        virtual uint32_t get_wlID(uint32_t pageID);
        virtual uint8_t get_pageType(uint32_t pageID);
        virtual uint32_t get_pagesize();
        virtual uint32_t get_pe();
        virtual uint32_t get_retention(uint32_t BlockID, uint64_t reqID);

        virtual std::vector<voltage> get_voltage(uint32_t pageID);
        virtual RETRYTABLE get_retry_table(bool flag);
        virtual bool setConfig(const char *name, const char *value);
        virtual uint32_t update(SimpleSSD::PAL::Config palConfig);
        virtual void check_layerdata();
        uint32_t is_retry;
        std::map<int, std::string> retentions_map;
        int max_retry;
        
    private:
        uint32_t block;
        uint32_t layer;
        uint32_t wl;
        uint32_t page;
        uint32_t PE;
        uint32_t pagesize;
        std::string bdifference_file;
        std::string ldifference_file;
        difference block_table;
        difference layer_table;
        ERRORTABLE error_table;
        std::string retention;
        uint8_t retention_mode;
        int NAND_type;    
        std::vector<uint32_t> retentions;
        std::map<uint32_t, float> retention_table;
        float mean;
        float sigma;
        
 };



}
}

#endif
