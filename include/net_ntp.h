#ifndef NET_NTP_H
#define NET_NTP_H

// NTP uses 1900 as the epoch, Unix uses 1970. Difference is 70 years in seconds.
#define NTP_TIMESTAMP_DELTA 2208988800ull
#define HOSTNAME "pool.ntp.org"

typedef struct {
    uint8_t li_vn_mode;      // LI (2b), VN (3b), Mode (3b)
    uint8_t stratum;         
    uint8_t poll;            
    uint8_t precision;       
    uint32_t rootDelay;      
    uint32_t rootDispersion; 
    uint32_t refId;          
    uint32_t refTm_s;        
    uint32_t refTm_f;        
    uint32_t origTm_s;       
    uint32_t origTm_f;       
    uint32_t rxTm_s;         
    uint32_t rxTm_f;         
    uint32_t txTm_s;         // Transmit Timestamp Seconds
    uint32_t txTm_f;         // Transmit Timestamp Fractions
} ntp_packet;


typedef struct {
    time_t time_data;		
    uint8_t leap_sec;
} ret_ntp;

ret_ntp *ntp_get();

#endif