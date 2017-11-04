#pragma once

#include <stdio.h>
#include <zlib.h>

#define APNG_API(ret) extern "C" __declspec(dllexport) ret __stdcall

#pragma comment (lib, "zlib.lib")
#pragma comment (lib, "libpng16.lib")

struct ApngEncoder {
	FILE* hFile;
	int width;
	int height;

	//context
	int frameCount;
	int seqIndex;
	int acTLPos;

	//temp
	z_stream op_zstream1;
	z_stream op_zstream2;
	unsigned int idat_size;
	unsigned int zbuf_size;

	unsigned char *zbuf;
	unsigned char *dest;
	unsigned char *row_buf;
	unsigned char *sub_row;
	unsigned char *up_row;
	unsigned char *avg_row;
	unsigned char *paeth_row;
};

enum struct ApngError : int {
	Success = 0,
	ContextCreateFailed = 1,
	FileError = 2,
	ArgumentError = 3,
	MemoryError = 4,
};

APNG_API(ApngError) apng_init(wchar_t *fileName, int width, int height, ApngEncoder **ppEnc);
APNG_API(ApngError) apng_append_frame(ApngEncoder *pEnc, void* pData, int x, int y, int width, int height, int stride, int delay_ms, bool optimize);
APNG_API(void) apng_write_end(ApngEncoder *pEnc);
APNG_API(void) apng_destroy(ApngEncoder **ppEnc);
