#include "retry.hh"
#include <cmath>
#include <stdlib.h>


namespace SimpleSSD{

namespace ERROR{

    void write_csv(std::string str){

        std::ofstream outfile;
        outfile.open("/mnt/g/kernal/github/simplessd-standalone/wdb.csv", std::ios::out | std::ios::trunc);
        outfile<<str<<std::endl;
        outfile.close();
    }

    bool Retry::is_Retry(CPDPBP addr, uint32_t retry, bool isopen, uint32_t retention){
        uint32_t error = get_ERROR(addr.Block, addr.Page, retry, isopen, retention);

        double UBER = ldpc.get_UBER(error);
        unsigned seed = std::chrono::steady_clock::now().time_since_epoch().count();
        srand(seed);
        double sj = rand() / double(RAND_MAX);

        // std::cout<<sj<<' '<< UBER<<std::endl;
        
        return sj <= UBER;

    }

    //  bool Retry::adjust_voltage(::PALasync *palasync, ::Command &cmd ,CPDPBP addr, uint32_t retention, SimpleSSD::PAL::Request *pReq,
    //                   uint64_t *pCounter){
    //     std::vector<voltage> v = config.get_voltage(addr.Page);
    //     bool flag = IsOpenBlock(Count::CPDPBP2Block(&addr));
    //     // std::cout<<flag<<std::endl;
    //     RETRYTABLE retry_table = config.get_retry_table(flag);

    //     // retryprint("Channel %u | Package %u | Die %u | Plane %u | Block %u | Page %u | tick: %lu, retention: %s, default error: %u", 
    //     //         addr.Channel, addr.Package, addr.Die, addr.Plane, addr.Block, addr.Page, cmd.arrived, config.retentions_map[retention].c_str(), get_ERROR(addr.Block, addr.Page, 0, flag, retention));
        
    //     Tick arriveTick = cmd.arrived;
    //     std::string str;
    //     for(uint32_t i=0;i<retry_table[v[0]].size();i++){
    //         // std::cout<<i<<":     ";
    //         str = "";
    //         for(uint32_t j=0;j<v.size();j++){
    //             // std::cout<<"R"<< v[j] + 1 <<":"<<retry_table[v[j]][i]<<"   ";
    //             str += "R" + std::to_string(v[j] + 1) + ":" + std::to_string(retry_table[v[j]][i])+ " ";
    //         }
    //         if(flag){
    //             count_open++;
    //             // std::cout<<"OPEN"<<std::endl;
    //             str += "OPEN";
    //         }else{
    //             count_close++;
    //             // std::cout<<"CLOSE"<<std::endl;
    //             str += "CLOSE";
    //         }

    //         retryStatistics.addRetry(addr,cmd.finished);
    //         cmd.arrived = cmd.finished;
    //         cmd.finished = 0;

    //         retryprint("Channel %u | Package %u | Die %u | Plane %u | Block %u | Page %u | tick: %lu, retention: %s, retry %u: %s error: %u", 
    //             addr.Channel, addr.Package, addr.Die, addr.Plane, addr.Block, addr.Page, cmd.arrived, config.retentions_map[retention].c_str(), i + 1, str.c_str(), get_ERROR(addr.Block, addr.Page, i+1, flag, retention));
            

            
    //         // std::vector<double> values;
    //         // palasync->getStatValues(values);
    //         // std::cout<<pReq<<" "<<pCounter<<" "<<values[0]<<std::endl;
    //         // palasync->submit(cmd, addr, pReq, pCounter);
    //         Tick latency = ldpc.get_latency(get_ERROR(addr.Block, addr.Page, i+1, flag, retention));
    //         cmd.finished += latency;
    //         //retry++
    //         if(!is_Retry(addr, i + 1, flag, retention)){
    //             retryprint(" ");
    //             cmd.arrived = arriveTick;
    //             return true;
    //         }
    //     }
    //     cmd.arrived = arriveTick;
    //     return false;
    // }

    uint32_t Retry::get_retention(uint32_t BlockID, uint64_t reqID){
        return config.get_retention(BlockID, reqID);
    }

    uint32_t Retry::get_ERROR(uint32_t blockID, uint32_t pageID, uint32_t retry, bool isopen, uint32_t retention){
        // std::cout<<"raw bit:"<<config.read_csv(config.get_pe(), config.get_retention(), pageID, retry)<<std::endl;
        double ratio;
        double mean_error;
        config.read_csv(config.get_pe(), retention, blockID, pageID, retry, ratio, mean_error, isopen);
        
        if(ratio < 0.5){
            return mean_error * 0.5 * config.get_pagesize();
        }
        else if(ratio < 0.8){
            return mean_error * 0.8 * config.get_pagesize();
        }
        else if(ratio < 1.2){
            return mean_error * config.get_pagesize();
        }
        else if(ratio < 2){
            return mean_error * 1.2 * config.get_pagesize();
        }
        else if(ratio < 3){
            return mean_error * 2 * config.get_pagesize();
        }
        else{
            return mean_error * 3 * config.get_pagesize();
        }
    }
}

}