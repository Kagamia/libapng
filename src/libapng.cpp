#include "libapng.h"
#include "WuQuantizer.h"
#include <png.h>
#include <zlib.h>

struct RECT {
	int x, y, width, height;
};

#pragma region Constants

#pragma endregion

#pragma region Function Declarations

void write_chunk(ApngEncoder *enc, const char *name, unsigned char *data, unsigned int length);
void write_IDATs(ApngEncoder *enc, unsigned char *data, unsigned int length, unsigned int idat_size);
void get_rect(const BitmapData *bmpData, RECT *rect);
void process_rect(ApngEncoder *pEnc, BitmapData *image, unsigned char *dest);
ApngError OptimizeImage(BitmapData *bmpData);
void deflate_rect_op(ApngEncoder *pEnc, BitmapData *image, bool *filter);
void deflate_rect_fin(ApngEncoder *pEnc, BitmapData *image, bool filter, unsigned int *zsize);
#pragma endregion


APNG_API(ApngError) apng_init(wchar_t *fileName, int width, int height, ApngEncoder **ppEnc)
{
	ApngError err;
	ApngEncoder *pEnc = (ApngEncoder*)calloc(1, sizeof(ApngEncoder));
	if (!pEnc) {
		err = ApngError::ContextCreateFailed;
		goto __failed;
	}

	if (_wfopen_s(&pEnc->hFile, fileName, L"wb")) {
		err = ApngError::FileError;
		goto __failed;
	}

	pEnc->width = width;
	pEnc->height = height;
	pEnc->frameCount = 0;
	pEnc->seqIndex = 0;
	pEnc->acTLPos = -1;

	//zlib init
	pEnc->op_zstream1.data_type = Z_BINARY;
	pEnc->op_zstream1.zalloc = Z_NULL;
	pEnc->op_zstream1.zfree = Z_NULL;
	pEnc->op_zstream1.opaque = Z_NULL;
	auto r1 = deflateInit2(&pEnc->op_zstream1, Z_BEST_SPEED + 1, 8, 15, 8, Z_DEFAULT_STRATEGY);

	pEnc->op_zstream2.data_type = Z_BINARY;
	pEnc->op_zstream2.zalloc = Z_NULL;
	pEnc->op_zstream2.zfree = Z_NULL;
	pEnc->op_zstream2.opaque = Z_NULL;
	auto r2 = deflateInit2(&pEnc->op_zstream2, Z_BEST_SPEED + 1, 8, 15, 8, Z_FILTERED);

	*ppEnc = pEnc;
	return ApngError::Success;

__failed:
	apng_destroy(&pEnc);
	ppEnc = NULL;
	return err;
}

APNG_API(ApngError) apng_append_frame(ApngEncoder *pEnc, void* pData, int x, int y, int width, int height, int stride, int delay_ms, bool optimize)
{
	/* references:
	 * https://wiki.mozilla.org/APNG_Specification
	 * http://www.w3.org/TR/2003/REC-PNG-20031110
	 */
	ApngError err = ApngError::Success;

	if (pEnc->frameCount == 0)
	{
		if (!(x == 0 && y == 0 && width == pEnc->width && height == pEnc->height))
		{
			return ApngError::ArgumentError;
		}

		//png sign
		{
			static const unsigned char png_sign[8] = { 137,  80,  78,  71,  13,  10,  26,  10 };
			fwrite(png_sign, 1, 8, pEnc->hFile);
		}

		//IHDR
		{
			unsigned char buf_IHDR[13];
			png_save_uint_32(buf_IHDR, pEnc->width);
			png_save_uint_32(buf_IHDR + 4, pEnc->height);
			buf_IHDR[8] = 8; //color depth
			buf_IHDR[9] = 6; //color type, 6=rgba
			buf_IHDR[10] = 0; //compression
			buf_IHDR[11] = 0; //filter
			buf_IHDR[12] = 0; //interlace

			write_chunk(pEnc, "IHDR", buf_IHDR, 13);
		}

		//acTL
		{
			unsigned char buf_acTL[8];
			png_save_uint_32(buf_acTL, 0); //frames
			png_save_uint_32(buf_acTL + 4, 0); //loops

			pEnc->acTLPos = ftell(pEnc->hFile);
			write_chunk(pEnc, "acTL", buf_acTL, 8);
		}

		unsigned int rowbytes = pEnc->width * 4;
		unsigned int idat_size = (rowbytes + 1) * height;
		unsigned int zbuf_size = idat_size + ((idat_size + 7) >> 3) + ((idat_size + 63) >> 6) + 11;

		pEnc->idat_size = idat_size;
		pEnc->zbuf_size = zbuf_size;

		pEnc->zbuf = (unsigned char *)malloc(zbuf_size);
		pEnc->dest = (unsigned char *)malloc(idat_size);
		pEnc->row_buf = (unsigned char *)malloc(rowbytes + 1);
		pEnc->sub_row = (unsigned char *)malloc(rowbytes + 1);
		pEnc->up_row = (unsigned char *)malloc(rowbytes + 1);
		pEnc->avg_row = (unsigned char *)malloc(rowbytes + 1);
		pEnc->paeth_row = (unsigned char *)malloc(rowbytes + 1);

		if (!pEnc->zbuf
			|| !pEnc->dest
			|| !pEnc->row_buf
			|| !pEnc->sub_row
			|| !pEnc->up_row
			|| !pEnc->avg_row
			|| !pEnc->paeth_row) {
			return ApngError::MemoryError;
		}

		pEnc->row_buf[0] = 0;
		pEnc->sub_row[0] = 1;
		pEnc->up_row[0] = 2;
		pEnc->avg_row[0] = 3;
		pEnc->paeth_row[0] = 4;
	}

	BitmapData bmpData;
	bmpData.Width = width;
	bmpData.Height = height;
	bmpData.Stride = stride;
	bmpData.bpp = 4;
	bmpData.Scan0 = pData;

	if (pEnc->frameCount > 0) {
		RECT rect;
		get_rect(&bmpData, &rect);

		x += rect.x;
		y += rect.y;
		bmpData.Width = rect.width;
		bmpData.Height = rect.height;
		bmpData.Scan0 = (unsigned char*)pData + rect.y * bmpData.Stride + rect.x * bmpData.bpp;		
	}

	if (optimize) {
		ApngError err = OptimizeImage(&bmpData);
		if (err != ApngError::Success) {
			return err;
		}
	}
	else {
		unsigned char *pixels = (unsigned char *)malloc(bmpData.Width * bmpData.Height * bmpData.bpp);
		if (!pixels) {
			return ApngError::MemoryError;
		}
		unsigned char *pDest = pixels, *pRow = (unsigned char*)bmpData.Scan0;
		unsigned int rowbytes = bmpData.Width * bmpData.bpp;
		for (int y = 0; y < bmpData.Height; y++) {
			memcpy(pDest, pRow, rowbytes);
			pRow += bmpData.Stride;
			pDest += rowbytes;
		}
		bmpData.Scan0 = pixels;
	}

	//bgra->rgba
	{
		unsigned char *pColor = (unsigned char*)bmpData.Scan0;
		for (int y = 0; y < bmpData.Height; y++) {
			for (int x = 0; x < bmpData.Width; x++) {
				auto temp = pColor[0];
				pColor[0] = pColor[2];
				pColor[2] = temp;
				pColor += 4;
			}
		}
	}

	//compress
	bool filter;
	unsigned int zsize;
	deflate_rect_op(pEnc, &bmpData, &filter);
	deflate_rect_fin(pEnc, &bmpData, filter, &zsize);

	//fcTL
	{
		unsigned char buf_fcTL[26];
		png_save_uint_32(buf_fcTL, pEnc->seqIndex++);
		png_save_uint_32(buf_fcTL + 4, bmpData.Width);
		png_save_uint_32(buf_fcTL + 8, bmpData.Height);
		png_save_uint_32(buf_fcTL + 12, x);
		png_save_uint_32(buf_fcTL + 16, y);
		png_save_uint_16(buf_fcTL + 20, delay_ms);
		png_save_uint_16(buf_fcTL + 22, 1000);
		buf_fcTL[24] = PNG_DISPOSE_OP_BACKGROUND;
		buf_fcTL[25] = PNG_BLEND_OP_SOURCE;
		write_chunk(pEnc, "fcTL", buf_fcTL, 26);
	}

	//Idat
	write_IDATs(pEnc, pEnc->zbuf, zsize, pEnc->idat_size);

	if (optimize) {
		free(bmpData.Scan0);
	}

	pEnc->frameCount++;
	return ApngError::Success;
}

APNG_API(void) apng_write_end(ApngEncoder *pEnc)
{
	//fix acTL
	bool acTLFixed = false;
	if (pEnc->acTLPos > -1 && pEnc->frameCount > 0) {
		if (!fseek(pEnc->hFile, pEnc->acTLPos, SEEK_SET)) {
			unsigned char buf_acTL[8];
			png_save_uint_32(buf_acTL, pEnc->frameCount); //frames
			png_save_uint_32(buf_acTL + 4, 0); //loops

			pEnc->acTLPos = ftell(pEnc->hFile);
			write_chunk(pEnc, "acTL", buf_acTL, 8);
			acTLFixed = true;
		}
	}

	//write end
	if (!acTLFixed || !fseek(pEnc->hFile, 0, SEEK_END)) {
		static unsigned char buf_tEXt[33] = { 83, 111, 102, 116, 119, 97, 114, 101, 0, 108, 105, 98, 97, 112, 110, 103, 32, 102, 111, 114, 32, 87, 122, 67, 111, 109, 112, 97, 114, 101, 114, 82, 50 };
		write_chunk(pEnc, "tEXt", buf_tEXt, 33);

		write_chunk(pEnc, "IEND", NULL, 0);
	}
}

APNG_API(void) apng_destroy(ApngEncoder **ppEnc)
{
	if (!ppEnc)
		return;

	ApngEncoder *pEnc = *ppEnc;
	if (pEnc) {
		if (pEnc->hFile) {
			fclose(pEnc->hFile);
		}
		if (!pEnc->zbuf) {
			free(pEnc->zbuf);
		}
		if (!pEnc->dest) {
			free(pEnc->dest);
		}
		if (!pEnc->row_buf) {
			free(pEnc->row_buf);
		}
		if (!pEnc->sub_row) {
			free(pEnc->sub_row);
		}
		if (!pEnc->up_row) {
			free(pEnc->up_row);
		}
		if (!pEnc->avg_row) {
			free(pEnc->avg_row);
		}
		if (!pEnc->paeth_row) {
			free(pEnc->paeth_row);
		}
	}
	*ppEnc = NULL;
}


void write_chunk(ApngEncoder *enc, const char *name, unsigned char *data, unsigned int length)
{
	unsigned char buf[4];
	unsigned int crc = (unsigned int)crc32(0, Z_NULL, 0);
	FILE *f = enc->hFile;

	png_save_uint_32(buf, length);
	fwrite(buf, 1, 4, f);
	fwrite(name, 1, 4, f);
	crc = (unsigned int)crc32(crc, (const Bytef *)name, 4);

	if (memcmp(name, "fdAT", 4) == 0)
	{
		png_save_uint_32(buf, enc->seqIndex++);
		fwrite(buf, 1, 4, f);
		crc = (unsigned int)crc32(crc, buf, 4);
		length -= 4;
	}

	if (data != NULL && length > 0)
	{
		fwrite(data, 1, length, f);
		crc = (unsigned int)crc32(crc, data, length);
	}

	png_save_uint_32(buf, crc);
	fwrite(buf, 1, 4, f);
}

void write_IDATs(ApngEncoder *enc, unsigned char *data, unsigned int length, unsigned int idat_size)
{
	FILE *f = enc->hFile;
	unsigned char z_cmf = data[0];
	
	if ((z_cmf & 0x0f) == 8 && (z_cmf & 0xf0) <= 0x70)
	{
		if (length >= 2)
		{
			unsigned char z_cinfo = z_cmf >> 4;
			unsigned int half_z_window_size = 1 << (z_cinfo + 7);
			while (idat_size <= half_z_window_size && half_z_window_size >= 256)
			{
				z_cinfo--;
				half_z_window_size >>= 1;
			}
			z_cmf = (z_cmf & 0x0f) | (z_cinfo << 4);
			if (data[0] != z_cmf)
			{
				data[0] = z_cmf;
				data[1] &= 0xe0;
				data[1] += (unsigned char)(0x1f - ((z_cmf << 8) + data[1]) % 0x1f);
			}
		}
	}
	
	while (length > 0)
	{
		unsigned int ds = length;
		if (ds > 32768)
			ds = 32768;

		if (enc->frameCount == 0)
			write_chunk(enc, "IDAT", data, ds);
		else
			write_chunk(enc, "fdAT", data, ds + 4);

		data += ds;
		length -= ds;
	}
}

void get_rect(const BitmapData *bmpData, RECT *rect) {
	unsigned int x_min = bmpData->Width - 1;
	unsigned int y_min = bmpData->Height - 1;
	unsigned int x_max = 0;
	unsigned int y_max = 0;

	for (int y = 0, y1 = bmpData->Height; y < y1; y++) {
		unsigned char *pRow = ((unsigned char *)bmpData->Scan0 + y * bmpData->Stride);

		for (int x = 0, x1 = bmpData->Width; x < x1; x++) {
			if (pRow[3]) {
				if (x < x_min) x_min = x;
				if (x > x_max) x_max = x;
				if (y < y_min) y_min = y;
				if (y > y_max) y_max = y;
			}
			pRow+=4;
		}
	}
	if (x_min > x_max) {
		rect->x = rect->y = 0;
		rect->width = rect->height = 1;
	}
	else {
		rect->x = x_min;
		rect->y = y_min;
		rect->width = x_max - x_min + 1;
		rect->height = y_max - y_min + 1;
	}
}

ApngError OptimizeImage(BitmapData *bmpData) {
	ApngError err = ApngError::Success;
	IndexedBitmapData optData;
	optData.ColorCount = MaxColor;
	optData.Palette = (Pixel*)malloc(4 * MaxColor);
	optData.Data.Width = bmpData->Width;
	optData.Data.Height = bmpData->Height;
	optData.Data.Stride = bmpData->Width;
	optData.Data.bpp = 1;
	optData.Data.Scan0 = malloc(bmpData->Width * bmpData->Height);

	if (!optData.Palette || !optData.Data.Scan0) {
		err = ApngError::MemoryError;
		goto __end;
	}

	QuantizeImage(bmpData, &optData);

	//expand palette;
	unsigned int *pOptImg = (unsigned int *)malloc(4 * bmpData->Width * bmpData->Height);

	if (!pOptImg) {
		err = ApngError::MemoryError;
		goto __end;
	}

	for (int y = 0, y1 = bmpData->Height; y < y1; y++) {
		unsigned int *pRow = pOptImg + y * bmpData->Width;
		unsigned char *pIndex = (unsigned char *)optData.Data.Scan0 + y * bmpData->Width;
		for (int x = 0, x1 = bmpData->Width; x < x1; x++) {
			*pRow = *(unsigned int*)&(optData.Palette[*pIndex]);
			pRow++;
			pIndex++;
		}
	}

	bmpData->Scan0 = pOptImg;
	bmpData->Stride = 4 * bmpData->Width;

	__end:
	if (optData.Palette)
		free(optData.Palette);
	if (optData.Data.Scan0)
		free(optData.Data.Scan0);
	return err;
}

void process_rect(ApngEncoder *pEnc, BitmapData *image, unsigned char *dest)
{
	unsigned char *prev = NULL;
	unsigned char *dp = dest;
	unsigned char *out;
	int rowbytes = image->Width * image->bpp;
	int bpp = image->bpp;

	for (int y = 0, y1 = image->Height; y < y1; y++)
	{
		unsigned char *row = (unsigned char *)image->Scan0 + y * image->Stride;
		unsigned int sum;
		unsigned char *best_row = pEnc->row_buf;
		unsigned int mins = ((unsigned int)(-1)) >> 1;
		
		//filter:0
		sum = 0;
		out = pEnc->row_buf + 1;
		for (int i = 0; i < rowbytes; i++)
		{
			auto v = out[i] = row[i];
			sum += (v < 128) ? v : 256 - v;
		}
		mins = sum;

		//filter:1
		sum = 0;
		out = pEnc->sub_row + 1;
		
		for (int i = 0; i < bpp; i++)
		{
			auto v = out[i] = row[i];
			sum += (v < 128) ? v : 256 - v;
		}
		for (int i = bpp; i < rowbytes; i++)
		{
			auto v = out[i] = row[i] - row[i - bpp];
			sum += (v < 128) ? v : 256 - v;
			if (sum > mins) break;
		}
		if (sum < mins)
		{
			mins = sum;
			best_row = pEnc->sub_row;
		}
		
		if (prev)
		{
			//filter:2
			sum = 0;
			out = pEnc->up_row + 1;
			for (int i = 0; i < rowbytes; i++)
			{
				auto v = out[i] = row[i] - prev[i];
				sum += (v < 128) ? v : 256 - v;
				if (sum > mins) break;
			}
			if (sum < mins)
			{
				mins = sum;
				best_row = pEnc->up_row;
			}

			//filter:3
			sum = 0;
			out = pEnc->avg_row + 1;
			for (int i = 0; i < bpp; i++)
			{
				auto v = out[i] = row[i] - prev[i] / 2;
				sum += (v < 128) ? v : 256 - v;
			}
			for (int i = bpp; i < rowbytes; i++)
			{
				auto v = out[i] = row[i] - (prev[i] + row[i - bpp]) / 2;
				sum += (v < 128) ? v : 256 - v;
				if (sum > mins) break;
			}
			if (sum < mins)
			{
				mins = sum;
				best_row = pEnc->avg_row;
			}

			//filter:4
			sum = 0;
			out = pEnc->paeth_row + 1;
			for (int i = 0; i < bpp; i++)
			{
				auto v = out[i] = row[i] - prev[i];
				sum += (v < 128) ? v : 256 - v;
			}
			for (int i = bpp; i < rowbytes; i++)
			{
				int a, b, c, pa, pb, pc, p;

				a = row[i - bpp];
				b = prev[i];
				c = prev[i - bpp];
				p = b - c;
				pc = a - c;
				pa = abs(p);
				pb = abs(pc);
				pc = abs(p + pc);
				p = (pa <= pb && pa <= pc) ? a : (pb <= pc) ? b : c;
				auto v = out[i] = row[i] - p;
				sum += (v < 128) ? v : 256 - v;
				if (sum > mins) break;
			}
			if (sum < mins)
			{
				best_row = pEnc->paeth_row;
			}
		}

		if (dest == NULL)
		{
			// deflate_rect_op()
			pEnc->op_zstream1.next_in = pEnc->row_buf;
			pEnc->op_zstream1.avail_in = rowbytes + 1;
			auto r = deflate(&pEnc->op_zstream1, Z_NO_FLUSH);

			pEnc->op_zstream2.next_in = best_row;
			pEnc->op_zstream2.avail_in = rowbytes + 1;
			auto r2 = deflate(&pEnc->op_zstream2, Z_NO_FLUSH);
		}
		else
		{
			// deflate_rect_fin()
			memcpy(dp, best_row, rowbytes + 1);
			dp += rowbytes + 1;
		}

		prev = row;
	}
}

void deflate_rect_op(ApngEncoder *pEnc, BitmapData *image, bool *filter)
{
	pEnc->op_zstream1.data_type = Z_BINARY;
	pEnc->op_zstream1.next_out = pEnc->zbuf;
	pEnc->op_zstream1.avail_out = pEnc->zbuf_size;

	pEnc->op_zstream2.data_type = Z_BINARY;
	pEnc->op_zstream2.next_out = pEnc->zbuf;
	pEnc->op_zstream2.avail_out = pEnc->zbuf_size;

	process_rect(pEnc, image, NULL);

	deflate(&pEnc->op_zstream1, Z_FINISH);
	deflate(&pEnc->op_zstream2, Z_FINISH);

	if (pEnc->op_zstream1.total_out < pEnc->op_zstream2.total_out)
	{
		*filter = false;
	}
	else
	{
		*filter = true;
	}

	deflateReset(&pEnc->op_zstream1);
	deflateReset(&pEnc->op_zstream2);
}

void deflate_rect_fin(ApngEncoder *pEnc, BitmapData *image, bool filter, unsigned int *zsize)
{
	int rowbytes = image->bpp * image->Width;
	if (!filter)
	{
		unsigned char *dp = pEnc->dest;
		unsigned char *pData = (unsigned char *)image->Scan0;
		for (int y = 0, y1 = image->Height; y < y1; y++)
		{
			*dp++ = 0;
			memcpy(dp, pData, rowbytes);
			pData += image->Stride;
			dp += rowbytes;
		}
	}
	else
	{
		process_rect(pEnc, image, pEnc->dest);
	}

	z_stream fin_zstream;

	fin_zstream.data_type = Z_BINARY;
	fin_zstream.zalloc = Z_NULL;
	fin_zstream.zfree = Z_NULL;
	fin_zstream.opaque = Z_NULL;
	deflateInit2(&fin_zstream, Z_BEST_COMPRESSION, 8, 15, 8, filter ? Z_FILTERED : Z_DEFAULT_STRATEGY);

	fin_zstream.next_out = pEnc->zbuf;
	fin_zstream.avail_out = pEnc->zbuf_size;
	fin_zstream.next_in = pEnc->dest;
	fin_zstream.avail_in = image->Height * rowbytes;
	deflate(&fin_zstream, Z_FINISH);
	*zsize = (unsigned int)fin_zstream.total_out;
	deflateEnd(&fin_zstream);
}
