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

        std::list<Packet> pending;
        std::list<pair<SimpleSSD::PAL::Request, Packet>> outstandingQueue;

        Tick process_time;

        SimpleSSD::ConfigReader conf;
        SimpleSSD::PAL::PAL *pPAL;

        SimpleSSD::PAL::Request convPALRequest(Packet *pkt) {
            // TODO: req的全部信息应该包含到pkt中, 因为PAL无法查询FTL生成具体地址, 目前随意生成
            SimpleSSD::PAL::Request req(1);
            req.blockIndex = pkt->addr / 4096;
            req.pageIndex = 0;
            req.ioFlag.set();
            req.beginAt = 0;

            switch (pkt->type) {
            case RD:
            case NT_RD:
                req.reqType = SimpleSSD::RequestType::READ;
                break;
            case WT:
            case NT_WT:
                req.reqType = SimpleSSD::RequestType::WRITE;
                break;
            default:
                assert(0);
            }

            return req;
        } 

        void commit() {
            SimpleSSD::PAL::Request req(0);
            while (pPAL->commitToUpper(req)) {
                auto iter = outstandingQueue.begin();
                SimpleSSD::PAL::Request* curreq = &iter->first;
                bool find = false;

                while (iter != outstandingQueue.end()) {
                    if (curreq->reqID == req.reqID && curreq->reqSubID == req.reqSubID &&
                        curreq->ftlSubID == req.ftlSubID &&
                        curreq->blockIndex == req.blockIndex &&
                        curreq->pageIndex == req.pageIndex &&
                        curreq->reqType == req.reqType) {
                    
                        find = true;
                        outstandingQueue.erase(iter);
                        // TODO: send_pkt
                        Packet& pkt = iter->second;
                        pkt.is_rsp = true;
                        std::swap(pkt.src, pkt.from);
                        send_pkt(pkt);
                        break;
                    }
                    ++iter;
                    curreq = &iter->first;
                }

                if(find == false) {
                    assert(0);
                }
            }
        }

        void issue()
        {
            for (auto it = pending.begin(); it != pending.end();) {
                auto &pkt = *it;
                SimpleSSD::PAL::Request req = convPALRequest(&pkt);       
                // TODO
                outstandingQueue.push_back(make_pair(req, pkt));
                auto tick = req.beginAt;
                switch (req.reqType) {
                case SimpleSSD::RequestType::READ:
                    pPAL->read(req, tick);
                    break;
                case SimpleSSD::RequestType::WRITE:
                    pPAL->write(req, tick);
                    break;
                case SimpleSSD::RequestType::PALERASE:
                    pPAL->erase(req, tick);
                    break;
                default:
                    assert(0);
                    break;
                }
                it = pending.erase(it);
            }
        }

    public:
        SimpleSSDInterface(Simulation *sim, const Tick process_time, const Addr start,
                           const std::string &config_file,
                           std::string name = "SimpleSSDInterface")
            : Device(sim, name), start(start), process_time(process_time)
        {
            if (!conf.init(config_file)) {
                std::cerr << " Failed to open simulation configuration file!" << std::endl;
                assert(0);
            } 
            SimpleSSD::Count::InitOpCount(conf);

            pPAL = new SimpleSSD::PAL::PAL(conf);
            pPAL->setCommitCallback([this]() { commit(); });
        }

        Addr start_addr() const { return start; }

        void transit() override
        {
            Packet pkt = receive_pkt();
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