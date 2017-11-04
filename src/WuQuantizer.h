#pragma once

#include <stdint.h>
#include "quartTypes.h"

const int AlphaThreshold = 10;
const int MaxColor = 256;
const int Alpha = 3;
const int Red = 2;
const int Green = 1;
const int Blue = 0;
const int SideSize = 33;
const int MaxSideIndex = 32;
const int BitDepth = 32;

struct BitmapData {
	int Width;
	int Height;
	int Stride;
	int bpp;
	void* Scan0;
};

struct IndexedBitmapData {
	BitmapData Data;
	Pixel* Palette;
	int ColorCount;
};

void __stdcall QuantizeImage(const BitmapData *sourceImage, const IndexedBitmapData *destImage);


