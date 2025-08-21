#ifndef _ENCODING_H_
#define _ENCODING_H_

#include "phoenix/types.h"
#include <stddef.h>

typedef __u32 crc32_t;

#define LIB_CRC32_INIT 0xffffffff


extern crc32_t enc_crc32NextByte(crc32_t crc, __u8 byte);


extern crc32_t enc_crc32Finalize(crc32_t crc);


typedef struct {
	__u32 buf;
	int bits;
	char outBuf[3];
} lib_base64_ctx;


extern void enc_base64Init(lib_base64_ctx *ctx);


extern size_t enc_base64EncodeByte(lib_base64_ctx *ctx, const __u8 byte);


extern size_t enc_base64Finalize(lib_base64_ctx *ctx);

#endif /* _ENCODING_H_ */
