#pragma once
#include "simplessd-standalone/simplessd/pal/pal.hh"
#include "simplessd-standalone/simplessd/sim/config_reader.hh"
#include "def.hpp"
#include "device.hpp"
#include "utils.hpp"

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
            // TODO
        }

    public:
        SimpleSSDInterface(Topology *topology, const Tick process_time,
                           const Addr start,
                           const std::string &config_file,
                           const std::string &output_dir,
                           std::string name = "SimpleSSDInterface")
            : Device(topology, name), start(start), process_time(process_time)
        {
            SimpleSSD::ConfigReader conf;
            if (!conf.init(config_file)) {
                std::cerr << " Failed to open simulation configuration file!" << std::endl;
                assert(0);
            } 
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
                    Logger::debug() << name() << " receive packet " << pkt.id << " from "
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