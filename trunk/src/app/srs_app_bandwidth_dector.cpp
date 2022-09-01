//
// Copyright (c) 2013-2021 Winlin
//
// SPDX-License-Identifier: MIT
//

// #include <srs_app_mpegts_udp.hpp>
#include <srs_kernel_rtc_rtp.hpp>

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
#include <srs_app_bandwidth_dector.hpp>

#define SRS_BANDWIDTH_DECTOR_PT_REQ 1
#define SRS_BANDWIDTH_DECTOR_PT_RES 2
#ifdef SRS_BANDWIDTH_DECTOR
// SrsBandwidthDectorResponsePacket::SrsBandwidthDectorResponsePacket() {
    
// }

SrsBandwidthDectorResponsePacket::~SrsBandwidthDectorResponsePacket() {

}

SrsGb28181Config::SrsGb28181Config(SrsConfDirective* c)
{
    timeout = _srs_config->get_stream_caster_timeout(c);
}

SrsGb28181Config::~SrsGb28181Config()
{
}

std::string SrsBandwidthDectorResponsePacket::format_print() {
    stringstream ss;
    ss << "{"
        << "v=" << std::to_string(version) << "," 
        << "pt=" << std::to_string(payload_type) << "," 
        << "length=" << std::to_string(length) << "," 
        << "seq=" << std::to_string(sequence_number) << "," 
        << "send data length=" << std::to_string(send_data_length) << "," 
        << "send_timestamp=" << std::to_string(send_timestamp) << "," 
        << "recv_timestamp=" << std::to_string(recv_timestamp) 
        << "}";
    return ss.str();
}

void SrsBandwidthDectorResponsePacket::encode(SrsBandwidthDectorRequestPacket* pkt) {
    version = 0;
    payload_type = SRS_BANDWIDTH_DECTOR_PT_RES;
    length = 24; // fixed. seq(4Byte) + send data length(4Byte) + send timestamp(8Byte) + recv timestamp(8Byte)
    sequence_number = pkt->sequence_number;
    send_data_length = pkt->length;
    send_timestamp = pkt->timestamp;
    recv_timestamp = (srs_get_system_time() + 500 ) / 1000;
}

std::string SrsBandwidthDectorResponsePacket::to_string() 
{
    char buf[kRtpPacketSize];
    SrsBuffer stream(buf, sizeof(buf));

    stream.write_1bytes((version << 6) | payload_type); // v pt
    stream.write_3bytes(length); // length
    stream.write_4bytes(sequence_number); // sequence_number
    stream.write_4bytes(send_data_length); // send_data_length
    stream.write_8bytes(send_timestamp); // send_timestamp
    stream.write_8bytes(recv_timestamp); // recv_timestamp
    return std::string(stream.data(), stream.pos());
}

SrsBandwidthDectorOverUdp::SrsBandwidthDectorOverUdp(SrsConfDirective* c)
{
    pprint = SrsPithyPrint::create_caster();
}

SrsBandwidthDectorOverUdp::~SrsBandwidthDectorOverUdp()
{
    srs_freep(pprint);
}

void SrsBandwidthDectorOverUdp::set_stfd(srs_netfd_t fd) 
{
    lfd = fd;
}
#if 0
srs_error_t SrsBandwidthDectorOverUdp::on_udp_packet(const sockaddr* from, const int fromlen, char* buf, int nb_buf)
{
    srs_error_t err = srs_success;
    char address_string[64];
    char port_string[16];

    // TODO： Hexadecimal print first n bytes

    if(getnameinfo(from, fromlen, 
                   (char*)&address_string, sizeof(address_string),
                   (char*)&port_string, sizeof(port_string),
                   NI_NUMERICHOST|NI_NUMERICSERV)) {
        return srs_error_new(ERROR_SYSTEM_IP_INVALID, "bad address");
    }
    std::string peer_ip = std::string(address_string);
    int peer_port = atoi(port_string);
    if (nb_buf < SRS_BANDWIDTH_DECTOR_MIN_PACKET_SIZE) {
        srs_warn("udp: wait %s:%d packet %d bytes", peer_ip.c_str(), peer_port, nb_buf);
        return err;
    }

    SrsBuffer* stream = new SrsBuffer(buf, nb_buf);
    SrsAutoFree(SrsBuffer, stream);
    if ((err = pkt.decode(stream)) != srs_success) {
        return srs_error_wrap(err, "pkt decode err");
    }
    
    SrsBandwidthDectorResponsePacket response_pkt;
    response_pkt.encode(&pkt);
    std::string str = response_pkt.to_string();
    
    srs_trace("recv pkt: %s, response:%s", pkt.format_print().c_str(), response_pkt.format_print().c_str());

    int ret = srs_sendto(lfd, (char*)str.c_str(), (int)str.length(), from, fromlen, SRS_UTIME_NO_TIMEOUT);
    if (ret <= 0) {
        srs_trace("bandwidth dector: send_message falid (%d)", ret);
    }
    return err;
}
#endif

SrsBandwidthDectorRequestPacket::SrsBandwidthDectorRequestPacket() {

}

SrsBandwidthDectorRequestPacket::~SrsBandwidthDectorRequestPacket() {

}

// refs: https://rongcloud.yuque.com/iyw4vm/tgzi1r/epbdl3#zn6hj
srs_error_t SrsBandwidthDectorRequestPacket::decode(SrsBuffer* stream) {
    srs_error_t err = srs_success;
    
    // at least 16bytes header
    if (!stream->require(16)) {
        return srs_error_new(ERROR_RTP_HEADER_CORRUPT, "requires 16 only %d bytes", stream->left());
    }
    
    int8_t vv = stream->read_1bytes();
    version = (vv >> 6) & 0x03;
    payload_type = vv & 0x3f;
    if (version != 0 || payload_type != SRS_BANDWIDTH_DECTOR_PT_REQ) {
        return srs_error_new(ERROR_BANDWIDTH_DECTOR_RTP_INVALID, 
            "rtp v:%d pt:%d invalid", version, payload_type);
    }

    length = stream->read_3bytes();
    if ((uint32_t)stream->left() < length) {
        return srs_error_new(ERROR_BANDWIDTH_DECTOR_RTP_INVALID, 
            "requires payload length %d only %d bytes", length, stream->left());
    }

    sequence_number = stream->read_4bytes();

    timestamp = stream->read_8bytes();
    
    return err;
}
#if 0
std::string SrsBandwidthDectorRequestPacket::format_print() {
    stringstream ss;
    ss << "{"
        << "v=" << std::to_string((int)version) << ","
        << "pt=" << std::to_string((int)payload_type) << ","
        << "length=" << std::to_string((int)length) << ","
        << "seq=" << std::to_string(sequence_number) << ","
        << "timestamp=" << std::to_string(timestamp)
        << "}";
    return ss.str();
}
#endif

// SrsBandwidthDectorOverUdp::SrsBandwidthDectorOverUdp(SrsConfDirective* c)
// {
//     pprint = SrsPithyPrint::create_caster();
// }

// SrsBandwidthDectorOverUdp::~SrsBandwidthDectorOverUdp()
// {
//     srs_freep(pprint);
// }

// void SrsBandwidthDectorOverUdp::set_stfd(srs_netfd_t fd) 
// {
//     lfd = fd;
// }

srs_error_t SrsBandwidthDectorOverUdp::on_udp_packet(const sockaddr* from, const int fromlen, char* buf, int nb_buf)
{
    srs_error_t err = srs_success;
    char address_string[64];
    char port_string[16];

    // TODO： Hexadecimal print first n bytes

    _srs_bd_dector->recv_byte_plus(nb_buf);
    
    if(getnameinfo(from, fromlen, 
                   (char*)&address_string, sizeof(address_string),
                   (char*)&port_string, sizeof(port_string),
                   NI_NUMERICHOST|NI_NUMERICSERV)) {
        return srs_error_new(ERROR_SYSTEM_IP_INVALID, "bad address");
    }
    std::string peer_ip = std::string(address_string);
    int peer_port = atoi(port_string);
    if (nb_buf < SRS_BANDWIDTH_DECTOR_MIN_PACKET_SIZE) {
        srs_warn("udp: wait %s:%d packet %d bytes", peer_ip.c_str(), peer_port, nb_buf);
        return err;
    }

    SrsBuffer* stream = new SrsBuffer(buf, nb_buf);
    SrsAutoFree(SrsBuffer, stream);
    if ((err = pkt.decode(stream)) != srs_success) {
        return srs_error_wrap(err, "pkt decode err");
    }
    
    SrsBandwidthDectorResponsePacket* response_pkt = new SrsBandwidthDectorResponsePacket();
    response_pkt->encode(&pkt);
    SrsBandWidthDectorSender* sender = _srs_bd_dector->fetch_or_create_sender();
    if (sender == NULL) {
        srs_error("bd dector: failed to fetch sender");
        return err;
    }

    sender->init(lfd, from, fromlen);
    sender->insert_queue(response_pkt);
    sender->wakeup();
    return err;
}
#if 0
// refs: https://rongcloud.yuque.com/iyw4vm/tgzi1r/epbdl3#zn6hj
srs_error_t SrsBandwidthDectorRequestPacket::decode(SrsBuffer* stream) {
    srs_error_t err = srs_success;
    
    // at least 16bytes header
    if (!stream->require(16)) {
        return srs_error_new(ERROR_RTP_HEADER_CORRUPT, "requires 16 only %d bytes", stream->left());
    }
    
    int8_t vv = stream->read_1bytes();
    version = (vv >> 6) & 0x03;
    payload_type = vv & 0x3f;
    if (version != 0 || payload_type != SRS_BANDWIDTH_DECTOR_PT_REQ) {
        return srs_error_new(ERROR_BANDWIDTH_DECTOR_RTP_INVALID, 
            "rtp v:%d pt:%d invalid", version, payload_type);
    }

    length = stream->read_3bytes();
    if ((uint32_t)stream->left() < length) {
        return srs_error_new(ERROR_BANDWIDTH_DECTOR_RTP_INVALID, 
            "requires payload length %d only %d bytes", length, stream->left());
    }

    sequence_number = stream->read_4bytes();

    timestamp = stream->read_8bytes();
    
    return err;
}
#endif
std::string SrsBandwidthDectorRequestPacket::format_print() {
    stringstream ss;
    ss << "{"
        << "v=" << std::to_string((int)version) << ","
        << "pt=" << std::to_string((int)payload_type) << ","
        << "length=" << std::to_string((int)length) << ","
        << "seq=" << std::to_string(sequence_number) << ","
        << "timestamp=" << std::to_string(timestamp)
        << "}";
    return ss.str();
}

SrsBandwidthDectorResponsePacket::SrsBandwidthDectorResponsePacket() {
    
}

// SrsBandwidthDectorResponsePacket::~SrsBandwidthDectorResponsePacket() {

// }

// std::string SrsBandwidthDectorResponsePacket::format_print() {
//     stringstream ss;
//     ss << "{"
//         << "v=" << std::to_string(version) << "," 
//         << "pt=" << std::to_string(payload_type) << "," 
//         << "length=" << std::to_string(length) << "," 
//         << "seq=" << std::to_string(sequence_number) << "," 
//         << "send data length=" << std::to_string(send_data_length) << "," 
//         << "send_timestamp=" << std::to_string(send_timestamp) << "," 
//         << "recv_timestamp=" << std::to_string(recv_timestamp) 
//         << "}";
//     return ss.str();
// }

// void SrsBandwidthDectorResponsePacket::encode(SrsBandwidthDectorRequestPacket* pkt) {
//     version = 0;
//     payload_type = SRS_BANDWIDTH_DECTOR_PT_RES;
//     length = 24; // fixed. seq(4Byte) + send data length(4Byte) + send timestamp(8Byte) + recv timestamp(8Byte)
//     sequence_number = pkt->sequence_number;
//     send_data_length = pkt->length;
//     send_timestamp = pkt->timestamp;
//     recv_timestamp = (srs_get_system_time() + 500 ) / 1000;
// }

// std::string SrsBandwidthDectorResponsePacket::to_string() {
//     char buf[1500];
//     // char buf[kRtpPacketSize];
//     SrsBuffer stream(buf, sizeof(buf));

//     stream.write_1bytes((version << 6) | payload_type); // v pt
//     stream.write_3bytes(length); // length
//     stream.write_4bytes(sequence_number); // sequence_number
//     stream.write_4bytes(send_data_length); // send_data_length
//     stream.write_8bytes(send_timestamp); // send_timestamp
//     stream.write_8bytes(recv_timestamp); // recv_timestamp
//     return std::string(stream.data(), stream.pos());
// }


srs_error_t SrsBandWidthDectorSender::do_cycle() 
{
    srs_error_t err = srs_success;
    
    while (true) {
        if ((err = trd->pull()) != srs_success) {
            return srs_error_wrap(err, "bd sender worker");
        }

        if (tasks.empty()) {
            _srs_bd_dector->add_sender_to_free_list(this);
            bool expired = false;
            while (tasks.empty()) {
                if (expired) {
                    srs_info("SrsCoroutine sender expired, %dSeconds", timeout/SRS_UTIME_SECONDS);
                    return err;
                }
                
                srs_info(".............entry wait");
                waiting(timeout);
                srs_info(".............leave wait");
                expired = true;
            }
        }

        while (!tasks.empty()) {
            SrsBandwidthDectorResponsePacket* response = tasks.front();
            tasks.pop();
            SrsAutoFree(SrsBandwidthDectorResponsePacket, response);
            std::string str = response->to_string();
    
            srs_info("bd sender: response:%s", response->format_print().c_str());
            _srs_bd_dector->syscall_plus();
            int ret = srs_sendto(lfd, (char*)str.c_str(), (int)str.length(), &from, fromlen, SRS_UTIME_NO_TIMEOUT);
            if (ret <= 0) {
                srs_error("bandwidth dector: send_message falid (%d)", ret);
            }
            _srs_bd_dector->send_byte_plus((int)str.length());
        }
    }
    
    return err;
}

string SrsBandWidthDectorSender::remote_ip() 
{
    return "";
}

const SrsContextId& SrsBandWidthDectorSender::get_id()
{
    return _srs_context->get_id();
}

std::string SrsBandWidthDectorSender::desc()
{
    return "BandWidthDectorSender";
}

SrsBandWidthDectorManger::SrsBandWidthDectorManger(SrsConfDirective* c)
{
    free_sender_cnt = 0;
    busy_sender_cnt = 0;
    config = new SrsGb28181Config(c);
    manager = new SrsResourceManager("BandWidthDector");
}

SrsBandWidthDectorManger::~SrsBandWidthDectorManger()
{
    // freep senders. no need to free.
    srs_freep(config);
    srs_freep(manager);
}

srs_error_t SrsBandWidthDectorManger::initialize()
{
    srs_error_t err = srs_success;
    if ((err = manager->start()) != srs_success) {
        return srs_error_wrap(err, "start manager");
    }
  
    return err;
}

SrsBandWidthDectorSender* SrsBandWidthDectorManger::fetch_or_create_sender()
{
    SrsBandWidthDectorSender* sender = NULL;
    if (senders.empty()) {
        sender = new SrsBandWidthDectorSender(config);
        sender->start();
        busy_sender_cnt++;
        srs_info("bd dector: creater new sender, busy cnt:%llu free cnt:%llu",
            busy_sender_cnt, free_sender_cnt);
        return sender;
    }

    sender = *(senders.begin());
    senders.erase(senders.begin());
    
    free_sender_cnt--;
    busy_sender_cnt++;
    return sender;
}

void SrsBandWidthDectorManger::add_sender_to_free_list(SrsBandWidthDectorSender* sender)
{
    free_sender_cnt++;
    busy_sender_cnt--;
    senders.insert(sender);
}

void SrsBandWidthDectorManger::remove_sender(SrsBandWidthDectorSender* sender)
{
    free_sender_cnt--;
    // busy_sender_cnt--;
    senders.erase(sender);
    manager->remove(sender);
}

void SrsBandWidthDectorManger::report_sender_cnt()
{
    srs_trace("bd dector: sender report, free cnt:%llu busy cnt:%llu"
        " syscal:%llu recvByte:%llu sendByte:%llu",
        free_sender_cnt, busy_sender_cnt, 
        syscall_cnt, recv_byte, send_byte);
}

void SrsBandWidthDectorManger::clear_statistics()
{
    syscall_cnt = 0;
    recv_byte = 0;
    send_byte = 0;
}

void SrsBandWidthDectorSender::waiting(srs_utime_t timeout){
    mw_waiting = true;
    srs_cond_timedwait(wait, timeout);
}

SrsBandWidthDectorSender::SrsBandWidthDectorSender(SrsGb28181Config *config)
{
    // trd = new SrsSTCoroutine("bandwidth_dector_sender", this);
    wait = srs_cond_new();
    lock = srs_mutex_new();
    timeout = config->timeout;
}

srs_error_t SrsBandWidthDectorSender::start() 
{
    srs_error_t err = srs_success;
    
    trd = new SrsSTCoroutine("bandwidth_dector_sender", this, _srs_context->get_id());
    if ((err = trd->start()) != srs_success) {
        return srs_error_wrap(err, "coroutine");
    }

    return err;
}

SrsBandWidthDectorSender::~SrsBandWidthDectorSender()
{
    srs_info("-------------------free sender");

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
    srs_info("bandwidth dector sender exit");
    _srs_bd_dector->remove_sender(this);
    
    if (err == srs_success) {
        srs_info("bw sender: client finished.");
    } else if (srs_is_client_gracefully_close(err)) {
        srs_info("bw sender: client disconnect peer. code=%d", srs_error_code(err));
        srs_freep(err);
    }
    
    return err;
}
#endif