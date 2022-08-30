//
// Copyright (c) 2013-2021 Winlin
//
// SPDX-License-Identifier: MIT
//

#include <srs_app_mpegts_udp.hpp>

#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
using namespace std;

#include <srs_app_config.hpp>
#include <srs_kernel_error.hpp>
#include <srs_kernel_log.hpp>
#include <srs_app_config.hpp>
#include <srs_kernel_ts.hpp>
#include <srs_kernel_buffer.hpp>
#include <srs_kernel_ts.hpp>
#include <srs_kernel_stream.hpp>
#include <srs_kernel_file.hpp>
#include <srs_core_autofree.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_rtmp_stack.hpp>
#include <srs_app_st.hpp>
#include <srs_protocol_utility.hpp>
#include <srs_app_utility.hpp>
#include <srs_protocol_amf0.hpp>
#include <srs_raw_avc.hpp>
#include <srs_app_pithy_print.hpp>
#include <srs_app_rtmp_conn.hpp>
#include <srs_protocol_utility.hpp>
#include <set>
#include <unordered_set>
#include <queue>

// refs: https://rongcloud.yuque.com/iyw4vm/tgzi1r/epbdl3#zn6hj
class SrsBandwidthDectorRequestPacket {
public:
    int8_t version; // 2bit fixed 00
    int8_t payload_type; // 6bit 1: request pkt, 2: response pkt
    uint32_t length; // 24bit 
    uint32_t sequence_number; // 32bit
    uint64_t timestamp; // 64bit
    // The payload.
    SrsSimpleStream* payload;

public:
    SrsBandwidthDectorRequestPacket();
    virtual ~SrsBandwidthDectorRequestPacket();
    virtual srs_error_t decode(SrsBuffer* stream);
    virtual std::string format_print();
};

// refs: https://rongcloud.yuque.com/iyw4vm/tgzi1r/epbdl3#zn6hj
class SrsBandwidthDectorResponsePacket {
private:
    int8_t version; // 2bit fixed 00
    int8_t payload_type; // 6bit 1: request pkt, 2: response pkt
    uint32_t length; // 24bit 
    uint32_t sequence_number; // 32bit
    uint32_t send_data_length; // 32bit
    uint64_t send_timestamp; // 64bit
    uint64_t recv_timestamp; // 64bit
    // The payload.
    // SrsSimpleStream* payload;
public:
    SrsBandwidthDectorResponsePacket();
    virtual ~SrsBandwidthDectorResponsePacket();
    virtual void encode(SrsBandwidthDectorRequestPacket* pkt);
    virtual std::string to_string();
    virtual std::string format_print();
};

// The brandwidth dector over udp stream caster.
class SrsBandwidthDectorOverUdp :  public ISrsUdpHandler
{
private:
    // SrsTsContext* context;
    // SrsSimpleStream* buffer;
    std::string output;
    SrsBandwidthDectorRequestPacket pkt;
    srs_netfd_t lfd;
    SrsPithyPrint* pprint;
public:
    virtual void set_stfd(srs_netfd_t fd);
    SrsBandwidthDectorOverUdp(SrsConfDirective* c);
    virtual ~SrsBandwidthDectorOverUdp();

public:
    virtual srs_error_t on_udp_packet(const sockaddr* from, const int fromlen, char* buf, int nb_buf);
};

class SrsGb28181Config
{
private:
    /* data */
public:
    SrsGb28181Config(/* args */);
    ~SrsGb28181Config();
};

SrsGb28181Config::SrsGb28181Config(/* args */)
{
}

SrsGb28181Config::~SrsGb28181Config()
{
}

class SrsBandWidthDectorSender : public ISrsCoroutineHandler
{
private:
    SrsCoroutine* trd;
    srs_netfd_t lfd;
    sockaddr from;
    int fromlen;
    std::queue<SrsBandwidthDectorResponsePacket*> tasks;
    srs_cond_t wait;
    srs_mutex_t lock;
    bool mw_waiting;
public:
    SrsBandWidthDectorSender(/* args */);
    ~SrsBandWidthDectorSender();
    void insert_queue(SrsBandwidthDectorResponsePacket* response);
    void wakeup();
    virtual srs_error_t start();
    virtual void init(srs_netfd_t lfd_, const sockaddr* from_, int fromlen_);
    virtual srs_error_t cycle();
    virtual srs_error_t do_cycle();
};

SrsBandWidthDectorSender::SrsBandWidthDectorSender(/* args */)
{
    trd = new SrsSTCoroutine("bandwidth_dector_sender", this);
    wait = srs_cond_new();
    lock = srs_mutex_new();
}

srs_error_t SrsBandWidthDectorSender::start() 
{
    srs_error_t err = srs_success;
    
    srs_freep(trd);
    trd = new SrsSTCoroutine("bandwidth_dector_sender", this, _srs_context->get_id());
    
    if ((err = trd->start()) != srs_success) {
        return srs_error_wrap(err, "coroutine");
    }
    
    return err;
}

SrsBandWidthDectorSender::~SrsBandWidthDectorSender()
{
    srs_freep(trd);
    
    while (!tasks.empty()) {
        SrsBandwidthDectorResponsePacket* pkt = tasks.front();
        tasks.pop();
        srs_freep(pkt);
    }
    
    srs_cond_destroy(wait);
    srs_mutex_destroy(lock);
}

void SrsBandWidthDectorSender::wakeup()
{
// #ifdef SRS_PERF_QUEUE_COND_WAIT
    if (mw_waiting) {
        srs_cond_signal(wait);
        mw_waiting = false;
    }
// #endif
}

void SrsBandWidthDectorSender::init(srs_netfd_t lfd_, const sockaddr* from_, int fromlen_) {
    lfd = lfd_;
    from = *from_;
    fromlen = fromlen_;
}

void SrsBandWidthDectorSender::insert_queue(SrsBandwidthDectorResponsePacket* response)
{
    tasks.push(response);
}

srs_error_t SrsBandWidthDectorSender::cycle() 
{
    // serve the rtmp muxer.
    srs_error_t err = do_cycle();

    srs_trace("bandwidth dector sender exit");
    
    if (err == srs_success) {
        srs_trace("client finished.");
    } else if (srs_is_client_gracefully_close(err)) {
        srs_warn("client disconnect peer. code=%d", srs_error_code(err));
        srs_freep(err);
    }
   
    return err;
}

srs_error_t SrsBandWidthDectorSender::do_cycle() 
{
    srs_error_t err = srs_success;
    
    while (true) {
        if ((err = trd->pull()) != srs_success) {
            return srs_error_wrap(err, "bd sender worker");
        }
        
        if (tasks.empty()) {
            _srs_bd_dector->add_sender_to_free(this);
            srs_cond_wait(wait);
        }

        while (!tasks.empty()) {
            SrsBandwidthDectorResponsePacket* response = tasks.front();
            tasks.pop();
            SrsAutoFree(SrsBandwidthDectorResponsePacket, response);
            std::string str = response->to_string();
    
            srs_trace("bd sender: response:%s", response->format_print().c_str());

            int ret = srs_sendto(lfd, (char*)str.c_str(), (int)str.length(), &from, fromlen, SRS_UTIME_NO_TIMEOUT);
            if (ret <= 0) {
                srs_trace("brandwidth dector: send_message falid (%d)", ret);
            }
        }
    }
    
    return err;
}

// Global singleton instance.
extern SrsBandWidthDectorManger *_srs_bd_dector;

class SrsBandWidthDectorManger
{
private:
    /* data */
    uint64_t free_sender_cnt;
    uint64_t busy_sender_cnt;

    SrsGb28181Config *config;
    unordered_set<SrsBandWidthDectorSender*>  senders;

public:
    SrsBandWidthDectorManger(/* args */);
    ~SrsBandWidthDectorManger();
    SrsBandWidthDectorSender* fetch_or_create_sender();
    void add_sender_to_free(SrsBandWidthDectorSender*);

};

SrsBandWidthDectorManger::SrsBandWidthDectorManger(/* args */)
{
    free_sender_cnt = 0;
    busy_sender_cnt = 0;
}

SrsBandWidthDectorManger::~SrsBandWidthDectorManger()
{
    // freep senders. no need to free.
}

SrsBandWidthDectorSender* SrsBandWidthDectorManger::fetch_or_create_sender()
{
    SrsBandWidthDectorSender* sender = NULL;
    if (senders.empty()) {
        sender = new SrsBandWidthDectorSender();
        sender->start();
        busy_sender_cnt++;
        return sender;
    }

    sender = *(senders.begin());
    senders.erase(senders.begin());
    
    free_sender_cnt--;
    busy_sender_cnt++;
    return sender;
}

void SrsBandWidthDectorManger::add_sender_to_free(SrsBandWidthDectorSender* sender)
{
    free_sender_cnt++;
    busy_sender_cnt--;
    senders.insert(sender);
}