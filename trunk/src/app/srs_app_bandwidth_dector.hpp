//
// Copyright (c) 2013-2021 Winlin
//
// SPDX-License-Identifier: MIT
//
#ifdef SRS_BANDWIDTH_DECTOR

// #include <srs_app_mpegts_udp.hpp>

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
class SrsBrandwidthDectorRequestPacket {
public:
    int8_t version; // 2bit fixed 00
    int8_t payload_type; // 6bit 1: request pkt, 2: response pkt
    uint32_t length; // 24bit 
    uint32_t sequence_number; // 32bit
    uint64_t timestamp; // 64bit
    // The payload.
    SrsSimpleStream* payload;
public:
    SrsBrandwidthDectorRequestPacket();
    virtual ~SrsBrandwidthDectorRequestPacket();
    virtual srs_error_t decode(SrsBuffer* stream);
    virtual std::string format_print();
};

// refs: https://rongcloud.yuque.com/iyw4vm/tgzi1r/epbdl3#zn6hj
class SrsBrandwidthDectorResponsePacket {
private:
    int8_t version; // 2bit fixed 00
    int8_t payload_type; // 6bit 1: request pkt, 2: response pkt
    uint32_t length; // 24bit 
    uint32_t sequence_number; // 32bit
    uint32_t send_data_length; // 32bit
    uint64_t send_timestamp; // 64bit
    uint64_t recv_timestamp; // 64bit
public:
    SrsBrandwidthDectorResponsePacket();
    virtual ~SrsBrandwidthDectorResponsePacket();
    virtual void encode(SrsBrandwidthDectorRequestPacket* pkt);
    virtual std::string to_string();
    virtual std::string format_print();
};

// The brandwidth dector over udp stream caster.
class SrsBrandwidthDectorOverUdp : public ISrsUdpHandler
{
private:
    std::string output;
    SrsBrandwidthDectorRequestPacket pkt;
    srs_netfd_t lfd;

private:
    SrsPithyPrint* pprint;
public:
    virtual void set_stfd(srs_netfd_t fd);
    SrsBrandwidthDectorOverUdp(SrsConfDirective* c);
    virtual ~SrsBrandwidthDectorOverUdp();

public:
    virtual srs_error_t on_udp_packet(const sockaddr* from, const int fromlen, char* buf, int nb_buf);
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
    SrsGb28181Config(SrsConfDirective* c);
    ~SrsGb28181Config();
};


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

class SrsBandWidthDectorManger
{
private:
    /* data */
    uint64_t free_sender_cnt;
    uint64_t busy_sender_cnt;

    SrsGb28181Config *config;
    unordered_set<SrsBandWidthDectorSender*>  senders;

public:
    SrsBandWidthDectorManger(SrsConfDirective* c);
    ~SrsBandWidthDectorManger();
    SrsBandWidthDectorSender* fetch_or_create_sender();
    void add_sender_to_free(SrsBandWidthDectorSender*);

};

// Global singleton instance.
extern SrsBandWidthDectorManger *_srs_bd_dector;
#endif