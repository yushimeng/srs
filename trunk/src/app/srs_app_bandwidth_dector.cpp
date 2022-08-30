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
#include <srs_app_bandwidth_dector.hpp>

#define SRS_BRANDWIDTH_DECTOR_PT_REQ 1
#define SRS_BRANDWIDTH_DECTOR_PT_RES 2

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

srs_error_t SrsBandwidthDectorOverUdp::on_udp_packet(const sockaddr* from, const int fromlen, char* buf, int nb_buf)
{
    srs_error_t err = srs_success;
    char address_string[64];
    char port_string[16];

    srs_trace("recv buf:%s", buf);

    if(getnameinfo(from, fromlen, 
                   (char*)&address_string, sizeof(address_string),
                   (char*)&port_string, sizeof(port_string),
                   NI_NUMERICHOST|NI_NUMERICSERV)) {
        return srs_error_new(ERROR_SYSTEM_IP_INVALID, "bad address");
    }
    std::string peer_ip = std::string(address_string);
    int peer_port = atoi(port_string);
    if (nb_buf < SRS_BRANDWIDTH_DECTOR_MIN_PACKET_SIZE) {
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
    if (version != 0 || payload_type != SRS_BRANDWIDTH_DECTOR_PT_REQ) {
        return srs_error_new(ERROR_BRANDWIDTH_DECTOR_RTP_INVALID, 
            "rtp v:%d pt:%d invalid", version, payload_type);
    }

    length = stream->read_3bytes();
    if ((uint32_t)stream->left() < length) {
        return srs_error_new(ERROR_BRANDWIDTH_DECTOR_RTP_INVALID, 
            "requires payload length %d only %d bytes", length, stream->left());
    }

    sequence_number = stream->read_4bytes();

    timestamp = stream->read_8bytes();
    
    return err;
}

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

SrsBandwidthDectorResponsePacket::~SrsBandwidthDectorResponsePacket() {

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
    payload_type = SRS_BRANDWIDTH_DECTOR_PT_RES;
    length = 24; // fixed. seq(4Byte) + send data length(4Byte) + send timestamp(8Byte) + recv timestamp(8Byte)
    sequence_number = pkt->sequence_number;
    send_data_length = pkt->length;
    send_timestamp = pkt->timestamp;
    recv_timestamp = (srs_get_system_time() + 500 ) / 1000;
}

std::string SrsBandwidthDectorResponsePacket::to_string() {
    char buf[1500];
    // char buf[kRtpPacketSize];
    SrsBuffer stream(buf, sizeof(buf));

    stream.write_1bytes((version << 6) | payload_type); // v pt
    stream.write_3bytes(length); // length
    stream.write_4bytes(sequence_number); // sequence_number
    stream.write_4bytes(send_data_length); // send_data_length
    stream.write_8bytes(send_timestamp); // send_timestamp
    stream.write_8bytes(recv_timestamp); // recv_timestamp
    return std::string(stream.data(), stream.pos());
}