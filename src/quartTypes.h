#pragma once

#include <stdint.h>
#include <vector>
#include <memory>

#ifndef DEBUG
#define OPENMP
#endif


using namespace std;

struct Box;
struct CubeCut;
struct Lookup;
struct Pixel;
struct PixelIndex;


struct Box
{
	uint8_t AlphaMinimum;
	uint8_t AlphaMaximum;
	uint8_t RedMinimum;
	uint8_t RedMaximum;
	uint8_t GreenMinimum;
	uint8_t GreenMaximum;
	uint8_t BlueMinimum;
	uint8_t BlueMaximum;
	int32_t Size;
};

template<int dataGranularity>
class ColorData
{
public:
	ColorData()
	{
		Weights = new int64_t[dataGranularity + 1][dataGranularity + 1][dataGranularity + 1][dataGranularity + 1];
		MomentsAlpha = new int64_t[dataGranularity + 1][dataGranularity + 1][dataGranularity + 1][dataGranularity + 1];
		MomentsRed = new int64_t[dataGranularity + 1][dataGranularity + 1][dataGranularity + 1][dataGranularity + 1];
		MomentsGreen = new int64_t[dataGranularity + 1][dataGranularity + 1][dataGranularity + 1][dataGranularity + 1];
		MomentsBlue = new int64_t[dataGranularity + 1][dataGranularity + 1][dataGranularity + 1][dataGranularity + 1];
		Moments = new float[dataGranularity + 1][dataGranularity + 1][dataGranularity + 1][dataGranularity + 1];
		QuantizedPixels = vector<PixelIndex>();
		Pixels = vector<Pixel>();

#pragma omp parallel sections
		{
#pragma omp section
			{memset(Weights, 0, sizeof(int64_t)*(dataGranularity + 1)*(dataGranularity + 1)*(dataGranularity + 1)*(dataGranularity + 1)); }
#pragma omp section
			{memset(MomentsAlpha, 0, sizeof(int64_t)*(dataGranularity + 1)*(dataGranularity + 1)*(dataGranularity + 1)*(dataGranularity + 1)); }
#pragma omp section
			{memset(MomentsRed, 0, sizeof(int64_t)*(dataGranularity + 1)*(dataGranularity + 1)*(dataGranularity + 1)*(dataGranularity + 1)); }
#pragma omp section
			{memset(MomentsGreen, 0, sizeof(int64_t)*(dataGranularity + 1)*(dataGranularity + 1)*(dataGranularity + 1)*(dataGranularity + 1)); }
#pragma omp section
			{memset(MomentsBlue, 0, sizeof(int64_t)*(dataGranularity + 1)*(dataGranularity + 1)*(dataGranularity + 1)*(dataGranularity + 1)); }
#pragma omp section
			{memset(Moments, 0, sizeof(float)*(dataGranularity + 1)*(dataGranularity + 1)*(dataGranularity + 1)*(dataGranularity + 1)); }
		}
	}

	ColorData(ColorData &&other)
		: Weights(move(other.Weights)),
		MomentsAlpha(move(other.MomentsAlpha)),
		MomentsRed(move(other.MomentsRed)),
		MomentsGreen(move(other.MomentsGreen)),
		MomentsBlue(move(other.MomentsBlue)),
		Moments(move(other.Moments)),
		QuantizedPixels(move(other.QuantizedPixels)),
		Pixels(move(other.Pixels))
	{
		other.Weights = NULL;
		other.MomentsAlpha = NULL;
		other.MomentsRed = NULL;
		other.MomentsGreen = NULL;
		other.MomentsBlue = NULL;
		other.Moments = NULL;
	}

	int64_t (*Weights)[dataGranularity + 1][dataGranularity + 1][dataGranularity + 1];
	int64_t(*MomentsAlpha)[dataGranularity + 1][dataGranularity + 1][dataGranularity + 1];
	int64_t(*MomentsRed)[dataGranularity + 1][dataGranularity + 1][dataGranularity + 1];
	int64_t(*MomentsGreen)[dataGranularity + 1][dataGranularity + 1][dataGranularity + 1];
	int64_t(*MomentsBlue)[dataGranularity + 1][dataGranularity + 1][dataGranularity + 1];
	float(*Moments)[dataGranularity + 1][dataGranularity + 1][dataGranularity + 1];
	vector<PixelIndex> QuantizedPixels;
	vector<Pixel> Pixels;

	~ColorData() {
		delete[] Weights;
		delete[] MomentsAlpha;
		delete[] MomentsRed;
		delete[] MomentsGreen;
		delete[] MomentsBlue;
		delete[] Moments;
	}
};

struct CubeCut
{
	CubeCut(uint8_t cutPoint, bool hasCutPoint, float result)
		: Position(cutPoint), hasPosition(hasCutPoint), Value(result)
	{
	}

	uint8_t Position;
	bool hasPosition;
	float Value;
};

struct Lookup
{
	uint32_t Alpha;
	uint32_t Red;
	uint32_t Green;
	uint32_t Blue;
};

template<int granularity>
class LookupData
{
public:
	LookupData()
	{
		Lookups = vector<Lookup>();
		Tags = new PixelIndex[granularity][granularity][granularity][granularity];

		memset(Tags, 0, sizeof(PixelIndex)*granularity*granularity*granularity*granularity);
	}

	LookupData(LookupData &&other) :
		Lookups(move(other.Lookups)),
		Tags(move(other.Tags))
	{
		other.Tags = NULL;
	}

	vector<Lookup> Lookups;
	PixelIndex (*Tags)[granularity][granularity][granularity];

	~LookupData() {
		delete[] Tags;
	}
};

struct Pixel
{
	Pixel(uint8_t alpha, uint8_t red, uint8_t green, uint8_t blue)
	{
		Alpha = alpha;
		Red = red;
		Green = green;
		Blue = blue;
	}

	uint8_t Blue;
	uint8_t Green;
	uint8_t Red;
	uint8_t Alpha;
};

struct PixelIndex
{
	PixelIndex() : Value(0) {

	}

	PixelIndex(uint8_t alpha, uint8_t red, uint8_t green, uint8_t blue)
		: PixelValue(Pixel(alpha, red, green, blue))
	{
	}

	PixelIndex(uint32_t packValue) : Value(packValue) {
	}
	union {
		uint32_t Value;
		Pixel PixelValue;
	};
};

class QuantizedPalette
{
public:
	QuantizedPalette(int size)
	{
		Colors = vector<Pixel>();
		PixelIndex = new int[size] {0};
	}

	QuantizedPalette(QuantizedPalette &&other)
		: Colors(move(other.Colors)),
		PixelIndex(move(other.PixelIndex))
	{
		other.PixelIndex = NULL;
	}

	vector<Pixel> Colors;
	int* PixelIndex;

	~QuantizedPalette() {
		delete[] PixelIndex;
	}
};