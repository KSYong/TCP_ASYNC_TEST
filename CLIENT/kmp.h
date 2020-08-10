#pragma once

#ifndef __KMP_H__
#define __KMP_H__

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>

#define DATA_MAX_LEN 1024

typedef unsigned short ushort;

/// @struct kmp_hdr_t
/// @brief 통신을 위한 프로토콜 헤더 구조체, 총 20바이트  
typedef struct kmp_hdr_s kmp_hdr_t;
struct kmp_hdr_s{
    /// message protocol version
    uint8_t version; // 1 byte
    /// message header + body length
    uint32_t length:24; // 3bytes
    /// message flag
    uint8_t flag; // 1byte
    /// message command code
    uint32_t code:24; // 3bytes
    /// message application id
    uint32_t app_id; // 4bytes
    /// message hop-by-hop identifier
    uint32_t hop_id; // 4bytes
    /// message end-to-end identifier
    uint32_t end_id; // 4bytes
};

/// @struct kmp_t
/// @brief 통신을 위한 프로토콜 구조체 
typedef struct kmp_s kmp_t;
struct kmp_s{
    /// message header
    kmp_hdr_t hdr;
    /// message data
    char data[ DATA_MAX_LEN];
};

kmp_t* kmp_init();
void kmp_destroy( kmp_t *msg);
int kmp_get_msg_length( kmp_t *msg);
char* kmp_get_data( kmp_t *msg);
int kmp_set_msg( kmp_t *msg, uint8_t version, char *data, uint32_t code);
void kmp_print_msg( kmp_t *msg);

#endif
