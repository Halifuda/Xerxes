#include "block.hh"

namespace SimpleSSD{
namespace ERROR{

        Blockstate* blockstate = nullptr;

        void InitBlockState(ConfigReader conf){
            if(blockstate)
                delete blockstate;
            blockstate = new Blockstate;
            blockstate->setblock(conf.readUint(CONFIG_PAL, PAL::PAL_CHANNEL) * conf.readUint(CONFIG_PAL, PAL::PAL_PACKAGE) * 
            conf.readUint(CONFIG_PAL, PAL::NAND_DIE) * conf.readUint(CONFIG_PAL, PAL::NAND_PLANE) * 
            conf.readUint(CONFIG_PAL, PAL::NAND_BLOCK));
        }

        void Blockstate::setblock(uint32_t blocksize){
            block = new int[blocksize]();
        }

        void deInitBlockState(){
            if(blockstate){
                delete blockstate;
                blockstate = nullptr;
            }
        }

        void update_blockstate(::Command cmd, ::CPDPBP addr, PAL::Parameter param){
            blockstate->update_blockstate(cmd, addr, param);
        }

        bool IsOpenBlock(uint32_t blockID){
            if(blockstate->get_state(blockID) == 1){
                return true;
            }
            return false;
        }

        Blockstate::~Blockstate(){
            delete []block;
        }

        void Blockstate::update_blockstate(::Command cmd, ::CPDPBP addr, PAL::Parameter param){
            uint32_t blockID = Count::CPDPBP2Block(&addr);
            if(cmd.operation == OPER_WRITE){
                block[blockID] = 1;
                // string s = std::to_string(addr.Page);
                // retryprint(s.c_str());
                if(addr.Page == param.page - 1){
                    // std::cout<<"write:"<< block[blockID]<<std::endl;
                    block[blockID] = 0;
                }
            }
            else if(cmd.operation == OPER_ERASE){
                block[blockID] = 0;
            }
            // std::cout<<block[blockID]<<std::endl;
        }

        int Blockstate::get_state(uint32_t blockID){
            return block[blockID];          
        }


    }
}