#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string>
#include <sstream>
#include<sys/time.h>
using namespace std;
#include <assert.h>
#ifndef srs_assert
#define srs_assert(expression) assert(expression)
#endif
#define DEST_PORT 8688
#define DSET_IP_ADDRESS  "127.0.0.1"
class SrsBuffer
{
private:
    // current position at bytes.
    char* p;
    // the bytes data for buffer to read or write.
    char* bytes;
    // the total number of bytes.
    int nb_bytes;
public:
    // Create buffer with data b and size nn.
    // @remark User must free the data b.
    SrsBuffer(char* b, int nn);
    ~SrsBuffer();
public:
    // Copy the object, keep position of buffer.
    SrsBuffer* copy();
    // Get the data and head of buffer.
    //      current-bytes = head() = data() + pos()
    char* data();
    char* head();
    // Get the total size of buffer.
    //      left-bytes = size() - pos()
    int size();
    void set_size(int v);
    // Get the current buffer position.
    int pos();
    // Left bytes in buffer, total size() minus the current pos().
    int left();
    // Whether buffer is empty.
    bool empty();
    // Whether buffer is able to supply required size of bytes.
    // @remark User should check buffer by require then do read/write.
    // @remark Assert the required_size is not negative.
    bool require(int required_size);
public:
    // Skip some size.
    // @param size can be any value. positive to forward; negative to backward.
    // @remark to skip(pos()) to reset buffer.
    // @remark assert initialized, the data() not NULL.
    void skip(int size);
public:
    // Read 1bytes char from buffer.
    int8_t read_1bytes();
    // Read 2bytes int from buffer.
    int16_t read_2bytes();
    int16_t read_le2bytes();
    // Read 3bytes int from buffer.
    int32_t read_3bytes();
    int32_t read_le3bytes();
    // Read 4bytes int from buffer.
    int32_t read_4bytes();
    int32_t read_le4bytes();
    // Read 8bytes int from buffer.
    int64_t read_8bytes();
    int64_t read_le8bytes();
    // Read string from buffer, length specifies by param len.
    std::string read_string(int len);
    // Read bytes from buffer, length specifies by param len.
    void read_bytes(char* data, int size);
public:
    // Write 1bytes char to buffer.
    void write_1bytes(int8_t value);
    // Write 2bytes int to buffer.
    void write_2bytes(int16_t value);
    void write_le2bytes(int16_t value);
    // Write 4bytes int to buffer.
    void write_4bytes(int32_t value);
    void write_le4bytes(int32_t value);
    // Write 3bytes int to buffer.
    void write_3bytes(int32_t value);
    void write_le3bytes(int32_t value);
    // Write 8bytes int to buffer.
    void write_8bytes(int64_t value);
    void write_le8bytes(int64_t value);
    // Write string to buffer
    void write_string(std::string value);
    // Write bytes to buffer
    void write_bytes(char* data, int size);
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
    // SrsSimpleStream* payload;
    char payload[9];
public:
    std::string format_print() {
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
    SrsBrandwidthDectorRequestPacket(int seq) {
        version = 0;
        payload_type = 1;
        length = 20;
        sequence_number= seq;
        timeval now;
        
        if (gettimeofday(&now, NULL) < 0) {
            // srs_warn("gettimeofday failed, ignore");
            // return -1;
        }
        
        // we must convert the tv_sec/tv_usec to int64_t.
        int64_t now_ms = ((int64_t)now.tv_sec) * 1000 + (int64_t)(now.tv_usec + 500)/1000;
        timestamp = now_ms;
        printf("ts=%ld\n", timestamp);
        // payload = "12345678";
        memcpy(payload, "12345678", 8);
        payload[8] = '\0';
    };
    virtual ~SrsBrandwidthDectorRequestPacket(){

    }
    // virtual srs_error_t encode(SrsBuffer* stream);
    std::string encode() {
        char buf[1500];
        // char buf[kRtpPacketSize];
        SrsBuffer stream(buf, sizeof(buf));

        stream.write_1bytes(1); // v pt
        stream.write_3bytes(length); // length
        stream.write_4bytes(sequence_number); // sequence_number
        // stream.write_4bytes(send_data_length); // send_data_length
        stream.write_8bytes(timestamp); // send_timestamp
        stream.write_bytes(payload, 8); // recv_timestamp
        return std::string(stream.data(), stream.pos());
    }
};

SrsBuffer::SrsBuffer(char* b, int nn)
{
    p = bytes = b;
    nb_bytes = nn;
}

SrsBuffer::~SrsBuffer()
{
}

SrsBuffer* SrsBuffer::copy()
{
    SrsBuffer* cp = new SrsBuffer(bytes, nb_bytes);
    cp->p = p;
    return cp;
}

char* SrsBuffer::data()
{
    return bytes;
}

char* SrsBuffer::head()
{
    return p;
}

int SrsBuffer::size()
{
    return nb_bytes;
}

void SrsBuffer::set_size(int v)
{
    nb_bytes = v;
}

int SrsBuffer::pos()
{
    return (int)(p - bytes);
}

int SrsBuffer::left()
{
    return nb_bytes - (int)(p - bytes);
}

bool SrsBuffer::empty()
{
    return !bytes || (p >= bytes + nb_bytes);
}

bool SrsBuffer::require(int required_size)
{
    if (required_size < 0) {
        return false;
    }
    
    return required_size <= nb_bytes - (p - bytes);
}

void SrsBuffer::skip(int size)
{
    srs_assert(p);
    srs_assert(p + size >= bytes);
    srs_assert(p + size <= bytes + nb_bytes);
    
    p += size;
}

int8_t SrsBuffer::read_1bytes()
{
    srs_assert(require(1));
    
    return (int8_t)*p++;
}

int16_t SrsBuffer::read_2bytes()
{
    srs_assert(require(2));
    
    int16_t value;
    char* pp = (char*)&value;
    pp[1] = *p++;
    pp[0] = *p++;
    
    return value;
}

int16_t SrsBuffer::read_le2bytes()
{
    srs_assert(require(2));

    int16_t value;
    char* pp = (char*)&value;
    pp[0] = *p++;
    pp[1] = *p++;

    return value;
}

int32_t SrsBuffer::read_3bytes()
{
    srs_assert(require(3));
    
    int32_t value = 0x00;
    char* pp = (char*)&value;
    pp[2] = *p++;
    pp[1] = *p++;
    pp[0] = *p++;
    
    return value;
}

int32_t SrsBuffer::read_le3bytes()
{
    srs_assert(require(3));

    int32_t value = 0x00;
    char* pp = (char*)&value;
    pp[0] = *p++;
    pp[1] = *p++;
    pp[2] = *p++;

    return value;
}

int32_t SrsBuffer::read_4bytes()
{
    srs_assert(require(4));
    
    int32_t value;
    char* pp = (char*)&value;
    pp[3] = *p++;
    pp[2] = *p++;
    pp[1] = *p++;
    pp[0] = *p++;
    
    return value;
}

int32_t SrsBuffer::read_le4bytes()
{
    srs_assert(require(4));

    int32_t value;
    char* pp = (char*)&value;
    pp[0] = *p++;
    pp[1] = *p++;
    pp[2] = *p++;
    pp[3] = *p++;

    return value;
}

int64_t SrsBuffer::read_8bytes()
{
    srs_assert(require(8));
    
    int64_t value;
    char* pp = (char*)&value;
    pp[7] = *p++;
    pp[6] = *p++;
    pp[5] = *p++;
    pp[4] = *p++;
    pp[3] = *p++;
    pp[2] = *p++;
    pp[1] = *p++;
    pp[0] = *p++;
    
    return value;
}

int64_t SrsBuffer::read_le8bytes()
{
    srs_assert(require(8));

    int64_t value;
    char* pp = (char*)&value;
    pp[0] = *p++;
    pp[1] = *p++;
    pp[2] = *p++;
    pp[3] = *p++;
    pp[4] = *p++;
    pp[5] = *p++;
    pp[6] = *p++;
    pp[7] = *p++;

    return value;
}

string SrsBuffer::read_string(int len)
{
    srs_assert(require(len));
    
    std::string value;
    value.append(p, len);
    
    p += len;
    
    return value;
}

void SrsBuffer::read_bytes(char* data, int size)
{
    srs_assert(require(size));
    
    memcpy(data, p, size);
    
    p += size;
}

void SrsBuffer::write_1bytes(int8_t value)
{
    srs_assert(require(1));
    
    *p++ = value;
}

void SrsBuffer::write_2bytes(int16_t value)
{
    srs_assert(require(2));
    
    char* pp = (char*)&value;
    *p++ = pp[1];
    *p++ = pp[0];
}

void SrsBuffer::write_le2bytes(int16_t value)
{
    srs_assert(require(2));

    char* pp = (char*)&value;
    *p++ = pp[0];
    *p++ = pp[1];
}

void SrsBuffer::write_4bytes(int32_t value)
{
    srs_assert(require(4));
    
    char* pp = (char*)&value;
    *p++ = pp[3];
    *p++ = pp[2];
    *p++ = pp[1];
    *p++ = pp[0];
}

void SrsBuffer::write_le4bytes(int32_t value)
{
    srs_assert(require(4));

    char* pp = (char*)&value;
    *p++ = pp[0];
    *p++ = pp[1];
    *p++ = pp[2];
    *p++ = pp[3];
}

void SrsBuffer::write_3bytes(int32_t value)
{
    srs_assert(require(3));
    
    char* pp = (char*)&value;
    *p++ = pp[2];
    *p++ = pp[1];
    *p++ = pp[0];
}

void SrsBuffer::write_le3bytes(int32_t value)
{
    srs_assert(require(3));

    char* pp = (char*)&value;
    *p++ = pp[0];
    *p++ = pp[1];
    *p++ = pp[2];
}

void SrsBuffer::write_8bytes(int64_t value)
{
    srs_assert(require(8));
    
    char* pp = (char*)&value;
    *p++ = pp[7];
    *p++ = pp[6];
    *p++ = pp[5];
    *p++ = pp[4];
    *p++ = pp[3];
    *p++ = pp[2];
    *p++ = pp[1];
    *p++ = pp[0];
}

void SrsBuffer::write_le8bytes(int64_t value)
{
    srs_assert(require(8));

    char* pp = (char*)&value;
    *p++ = pp[0];
    *p++ = pp[1];
    *p++ = pp[2];
    *p++ = pp[3];
    *p++ = pp[4];
    *p++ = pp[5];
    *p++ = pp[6];
    *p++ = pp[7];
}

void SrsBuffer::write_string(std::string value)
{
    srs_assert(require((int)value.length()));
    
    memcpy(p, value.data(), value.length());
    p += value.length();
}

void SrsBuffer::write_bytes(char* data, int size)
{
    srs_assert(require(size));
    
    memcpy(p, data, size);
    p += size;
}
// refs: https://rongcloud.yuque.com/iyw4vm/tgzi1r/epbdl3#zn6hj
class SrsBrandwidthDectorResponsePacket {
public:
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
    SrsBrandwidthDectorResponsePacket(){}
    virtual ~SrsBrandwidthDectorResponsePacket(){}
    // virtual void encode(SrsBrandwidthDectorRequestPacket* pkt){}
    virtual void encode(char* buf, int len) {
        SrsBuffer stream(buf, len);
        int8_t vv = stream.read_1bytes();
        version = (vv >> 6) & 0x03;
        payload_type = vv & 0x3f;

        length = stream.read_3bytes();
        sequence_number = stream.read_4bytes();
        send_data_length = stream.read_4bytes();
        send_timestamp = stream.read_8bytes();
        recv_timestamp = stream.read_8bytes();
    }
    // virtual std::string to_string();
    virtual std::string format_print() {
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
};
int main()
{
    /* socket文件描述符 */
    int sock_fd;

    /* 建立udp socket */
    sock_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if(sock_fd < 0)
    {
    perror("socket");
    exit(1);
    }

    /* 设置address */
    struct sockaddr_in addr_serv;
    int len;
    memset(&addr_serv, 0, sizeof(addr_serv));
    addr_serv.sin_family = AF_INET;
    addr_serv.sin_addr.s_addr = inet_addr(DSET_IP_ADDRESS);
    addr_serv.sin_port = htons(DEST_PORT);
    len = sizeof(addr_serv);


    int send_num;
    int recv_num;
    char send_buf[20] = "hey, who are you?";
    char recv_buf[1500];
    int seq = 1;
    int res_seq= 0;
    while (seq < 100000) {
        SrsBrandwidthDectorRequestPacket pkt(seq++);
        std::string str = pkt.encode();
        printf("client send: %s, %ld\n", str.c_str(), str.length());
        
        send_num = sendto(sock_fd, str.c_str(), str.length(), 0, (struct sockaddr *)&addr_serv, len);

        if(send_num < 0)
        {
            perror("sendto error:");
            exit(1);
        }

        recv_num = recvfrom(sock_fd, recv_buf, sizeof(recv_buf), 0, (struct sockaddr *)&addr_serv, (socklen_t *)&len);

        if(recv_num < 0)
        {
            perror("recvfrom error:");
            exit(1);
        }

        recv_buf[recv_num] = '\0';
        SrsBrandwidthDectorResponsePacket resp;
        resp.encode(recv_buf, recv_num);
        res_seq++;
        printf("client receive %d, %d bytes: %s\n",res_seq, recv_num, resp.format_print().c_str());
        // sleep(1);
    }
    close(sock_fd);

    return 0;
}