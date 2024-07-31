#pragma once
#include "simplessd/pal/pal.hh"
#include "simplessd/sim/config_reader.hh"
#include "simplessd/count/Counts.hh"
#include "def.hh"
#include "device.hh"
#include "utils.hh"

#include <list>
#include <map>

namespace xerxes
{

    class SimpleSSDInterface : public Device
    {
    private:
        Addr start;

        std::vector<Packet> pending;
        std::map<Addr, std::list<Packet>> issued;

        Tick process_time;

        SimpleSSD::PAL::PAL *pPAL;

        void issue()
        {
            std::vector<std::vector<Packet>::iterator> to_erase;
            for (auto it = pending.begin(); it != pending.end(); ++it) {
                auto &pkt = *it;
                // TODO: handle packet
                
                pending.erase(it);
            }
        }

    public:
        SimpleSSDInterface(Simulation *sim, const Tick process_time, const Addr start,
                           const std::string &config_file,
                           std::string name = "SimpleSSDInterface")
            : Device(sim, name), start(start), process_time(process_time)
        {
            SimpleSSD::ConfigReader conf;
            if (!conf.init(config_file)) {
                std::cerr << " Failed to open simulation configuration file!" << std::endl;
                assert(0);
            } 
            SimpleSSD::Count::InitOpCount(conf);

            pPAL = new SimpleSSD::PAL::PAL(conf);
        }

        Addr start_addr() const { return start; }

        void transit() override
        {
            auto pkt = receive_pkt();
            while (pkt.type != PacketType::PKT_TYPE_NUM)
            {
                if (pkt.dst == self)
                {
                    XerxesLogger::debug() << name() << " receive packet " << pkt.id << " from "
                                    << pkt.from << " at " << pkt.arrive << std::endl;
                    pkt.delta_stat(DEVICE_PROCESS_TIME, (double)(process_time));
                    pkt.arrive += process_time;
                    pending.push_back(pkt);
                }
                else
                {
                    send_pkt(pkt);
                }
                pkt = receive_pkt();
            }
            return issue();
        }
    };

} // namespace xerxes