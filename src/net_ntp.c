#include <arpa/inet.h>
#include <netdb.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#include "net_ntp.h"

ret_ntp *ntp_get() {
    int sockfd;
    struct sockaddr_in serv_addr;
    struct hostent *server;
    ntp_packet packet;
    //Time output
    ret_ntp *resp = malloc(sizeof(ret_ntp)); 
    
    // Initialize packet (Client mode = 3, Version = 4)
    memset(&packet, 0, sizeof(ntp_packet));
    packet.li_vn_mode = 0x23; // 00 100 011 (LI=0, VN=4, Mode=3) == 0010 0011

    // Setup Socket
    sockfd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    server = gethostbyname(HOSTNAME);
    
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    memcpy(&serv_addr.sin_addr.s_addr, server->h_addr_list[0], server->h_length);
    serv_addr.sin_port = htons(123);

    //Send Request & Receive Time
    sendto(sockfd, &packet, sizeof(ntp_packet), 0, (struct sockaddr*)&serv_addr, sizeof(serv_addr));
    recv(sockfd, &packet, sizeof(ntp_packet), 0);
    // Parse the Leap Indicator
    uint8_t li = (packet.li_vn_mode >> 6) & 0x03;
    // Convert Timestamp to Unix time
    // NTP is Big-Endian
    time_t txTm = (time_t)(ntohl(packet.txTm_s) - NTP_TIMESTAMP_DELTA);

    resp->time_data = txTm;
    resp->leap_sec = li;

    close(sockfd);
    return resp;
}