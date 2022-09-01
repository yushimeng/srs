//
// Copyright (c) 2013-2021 Winlin
//
// SPDX-License-Identifier: MIT
//
#ifdef SRS_BANDWIDTH_DECTOR

#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
using namespace std;

#include <srs_app_listener.hpp>
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
public:
    SrsBandwidthDectorResponsePacket();
    virtual ~SrsBandwidthDectorResponsePacket();
    virtual void encode(SrsBandwidthDectorRequestPacket* pkt);
    virtual std::string to_string();
    virtual std::string format_print();
};

// The bandwidth dector over udp stream caster.
class SrsBandwidthDectorOverUdp : public ISrsUdpHandler
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
public:
    /* data */
    srs_utime_t timeout;
public:
    SrsGb28181Config(SrsConfDirective* c);
    ~SrsGb28181Config();
};


class SrsBandWidthDectorSender : public ISrsCoroutineHandler, public ISrsConnection
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
    srs_utime_t timeout;
private:
    void waiting(srs_utime_t timeout = SRS_UTIME_NO_TIMEOUT);
public:
    SrsBandWidthDectorSender(SrsGb28181Config *config);
    ~SrsBandWidthDectorSender();
    virtual void insert_queue(SrsBandwidthDectorResponsePacket* response);
    virtual void wakeup();
    virtual srs_error_t start();
    virtual void init(srs_netfd_t lfd_, const sockaddr* from_, int fromlen_);
    virtual srs_error_t cycle();
    virtual srs_error_t do_cycle();
    virtual std::string remote_ip();
    virtual const SrsContextId& get_id();
    virtual std::string desc();
};

class SrsBandWidthDectorManger
{
private:
    /* data */
    uint64_t free_sender_cnt;
    uint64_t busy_sender_cnt;
    uint64_t syscall_cnt;
    uint64_t recv_byte;
    uint64_t send_byte;

    SrsGb28181Config *config;
    unordered_set<SrsBandWidthDectorSender*>  senders;
    SrsResourceManager* manager;
public:
    SrsBandWidthDectorManger(SrsConfDirective* c);
    ~SrsBandWidthDectorManger();
    virtual srs_error_t initialize();
    SrsBandWidthDectorSender* fetch_or_create_sender();
    void add_sender_to_free_list(SrsBandWidthDectorSender*);
    void remove_sender(SrsBandWidthDectorSender* sender);
    void report_sender_cnt();
    // 发送/接收/系统调用 统计数据清零
    void clear_statistics();
    void syscall_plus() { ++syscall_cnt; }
    void recv_byte_plus(int n) { recv_byte += n; }
    void send_byte_plus(int n) { send_byte += n; }

    uint64_t get_syscall_cnt() { return syscall_cnt; }
    uint64_t get_recv_bytes() { return recv_byte; }
    uint64_t get_send_bytes() { return send_byte; }
};

// Global singleton instance.
extern SrsBandWidthDectorManger *_srs_bd_dector;
#endif