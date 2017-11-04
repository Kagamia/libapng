#include <math.h>
#include <algorithm>
#include "WuQuantizer.h"


typedef ColorData<MaxSideIndex> _ColorData;
typedef LookupData<SideSize> _LookupData;

_ColorData BuildHistogram(const BitmapData *sourceImage);
void CalculateMoments(const _ColorData *data);
vector<Box> SplitData(int &colorCount, const _ColorData *data);
bool Cut(const _ColorData * data, Box & first, Box & second);
QuantizedPalette GetQuantizedPalette(int colorCount, _ColorData *data, const vector<Box> &cubes);
void ProcessImagePixels(const BitmapData *sourceImage, const QuantizedPalette *palette, const IndexedBitmapData *destImage);

CubeCut Maximize(const _ColorData * data, const Box &cube, int direction, uint8_t first, uint8_t last, int64_t wholeAlpha, int64_t wholeRed, int64_t wholeGreen, int64_t wholeBlue, int64_t wholeWeight);

template<int _1, int _2, int _3>
int64_t Volume(const Box &cube, int64_t(*moment)[_1][_2][_3]);

template<int _1, int _2, int _3>
float VolumeFloat(const Box &cube, float(*moment)[_1][_2][_3]);

template<int _1, int _2, int _3>
int64_t Top(const Box &cube, int direction, int position, int64_t(*moment)[_1][_2][_3]);

template<int _1, int _2, int _3>
int64_t Bottom(const Box &cube, int direction, int64_t(*moment)[_1][_2][_3]);

float CalculateVariance(const _ColorData *data, const Box &cube);

_LookupData BuildLookups(const vector<Box> &cubes, const _ColorData *data);


void __stdcall QuantizeImage(const BitmapData *sourceImage, const IndexedBitmapData *destImage)
{
	auto colorCount = MaxColor;
	auto data = BuildHistogram(sourceImage);
	CalculateMoments(&data);
	auto cubes = SplitData(colorCount, &data);
	auto palette = GetQuantizedPalette(colorCount, &data, cubes);
	ProcessImagePixels(sourceImage, &palette, destImage);
}

_ColorData BuildHistogram(const BitmapData *sourceImage) {
	_ColorData colorData;
	const BitmapData *data = sourceImage;

	int byteLength = data->Stride < 0 ? -data->Stride : data->Stride;
	const int byteCount = max(1, BitDepth >> 3);
	int offset = 0;

	size_t bufferSize = byteLength * sourceImage->Height;
	//uint8_t *buffer = new uint8_t[bufferSize];
	uint8_t value[byteCount] = { 0 };

	//memcpy_s(buffer, bufferSize, sourceImage->Scan0, sourceImage->Height * sourceImage->Stride);
	uint8_t *buffer = static_cast<uint8_t*>(sourceImage->Scan0);

	colorData.QuantizedPixels.reserve(sourceImage->Width * sourceImage->Height);
	colorData.Pixels.reserve(sourceImage->Width * sourceImage->Height);

	for (int y = 0, y1 = sourceImage->Height, x1 = sourceImage->Width; y < y1; y++)
	{
		int index = 0;
		for (int x = 0; x < x1; x++)
		{
			int indexOffset = index >> 3;

			for (int valueIndex = 0; valueIndex < byteCount; valueIndex++)
				value[valueIndex] = buffer[offset + valueIndex + indexOffset];

			uint8_t indexAlpha = ((value[Alpha] >> 3) + 1);
			uint8_t indexRed = ((value[Red] >> 3) + 1);
			uint8_t indexGreen = ((value[Green] >> 3) + 1);
			uint8_t indexBlue = ((value[Blue] >> 3) + 1);

			int alpha = value[Alpha];
			if (alpha > AlphaThreshold)
			{
				if (alpha < 255)
				{
					//var alpha = value[Alpha] + (value[Alpha] % 70);
					alpha = (alpha + 8) & 0xf0;
					int a = (alpha > 255 ? 255 : alpha);
					value[Alpha] = a;
					indexAlpha = ((a >> 3) + 1);
				}

				colorData.Weights[indexAlpha][indexRed][indexGreen][indexBlue]++;
				colorData.MomentsRed[indexAlpha][indexRed][indexGreen][indexBlue] += value[Red];
				colorData.MomentsGreen[indexAlpha][indexRed][indexGreen][indexBlue] += value[Green];
				colorData.MomentsBlue[indexAlpha][indexRed][indexGreen][indexBlue] += value[Blue];
				colorData.MomentsAlpha[indexAlpha][indexRed][indexGreen][indexBlue] += value[Alpha];
				colorData.Moments[indexAlpha][indexRed][indexGreen][indexBlue] += (value[Alpha] * value[Alpha]) +
					(value[Red] * value[Red]) +
					(value[Green] * value[Green]) +
					(value[Blue] * value[Blue]);
			}
			colorData.QuantizedPixels.push_back(PixelIndex(indexAlpha, indexRed, indexGreen, indexBlue));
			colorData.Pixels.push_back(Pixel(value[Alpha], value[Red], value[Green], value[Blue]));
			index += BitDepth;
		}
		offset += byteLength;
	}
	return colorData;
}

void CalculateMoments(const _ColorData *data) {

	auto xarea = new int64_t[SideSize][SideSize][SideSize];
	auto xareaAlpha = new int64_t[SideSize][SideSize][SideSize];
	auto xareaRed = new int64_t[SideSize][SideSize][SideSize];
	auto xareaGreen = new int64_t[SideSize][SideSize][SideSize];
	auto xareaBlue = new int64_t[SideSize][SideSize][SideSize];
	auto xarea2 = new float[SideSize][SideSize][SideSize];

	for (int alphaIndex = 1; alphaIndex <= MaxSideIndex; ++alphaIndex)
	{
		memset(xarea, 0, sizeof(int64_t)*SideSize*SideSize*SideSize);
		memset(xareaAlpha, 0, sizeof(int64_t)*SideSize*SideSize*SideSize);
		memset(xareaRed, 0, sizeof(int64_t)*SideSize*SideSize*SideSize);
		memset(xareaGreen, 0, sizeof(int64_t)*SideSize*SideSize*SideSize);
		memset(xareaBlue, 0, sizeof(int64_t)*SideSize*SideSize*SideSize);
		memset(xarea2, 0, sizeof(float)*SideSize*SideSize*SideSize);

		for (int redIndex = 1; redIndex <= MaxSideIndex; ++redIndex)
		{
			int64_t area[SideSize] = { 0 };
			int64_t areaAlpha[SideSize] = { 0 };
			int64_t areaRed[SideSize] = { 0 };
			int64_t areaGreen[SideSize] = { 0 };
			int64_t areaBlue[SideSize] = { 0 };
			float area2[SideSize] = { 0 };
			for (int greenIndex = 1; greenIndex <= MaxSideIndex; ++greenIndex)
			{
				int64_t line = 0;
				int64_t lineAlpha = 0;
				int64_t lineRed = 0;
				int64_t lineGreen = 0;
				int64_t lineBlue = 0;
				float line2 = 0.0f;
				for (int blueIndex = 1; blueIndex <= MaxSideIndex; ++blueIndex)
				{
					
					line += data->Weights[alphaIndex][redIndex][greenIndex][blueIndex];
					lineAlpha += data->MomentsAlpha[alphaIndex][redIndex][greenIndex][blueIndex];
					lineRed += data->MomentsRed[alphaIndex][redIndex][greenIndex][blueIndex];
					lineGreen += data->MomentsGreen[alphaIndex][redIndex][greenIndex][blueIndex];
					lineBlue += data->MomentsBlue[alphaIndex][redIndex][greenIndex][blueIndex];
					line2 += data->Moments[alphaIndex][redIndex][greenIndex][blueIndex];

					area[blueIndex] += line;
					areaAlpha[blueIndex] += lineAlpha;
					areaRed[blueIndex] += lineRed;
					areaGreen[blueIndex] += lineGreen;
					areaBlue[blueIndex] += lineBlue;
					area2[blueIndex] += line2;

					xarea[redIndex][greenIndex][blueIndex] = xarea[redIndex - 1][greenIndex][blueIndex] + area[blueIndex];
					xareaAlpha[redIndex][greenIndex][blueIndex] = xareaAlpha[redIndex - 1][greenIndex][blueIndex] + areaAlpha[blueIndex];
					xareaRed[redIndex][greenIndex][blueIndex] = xareaRed[redIndex - 1][greenIndex][blueIndex] + areaRed[blueIndex];
					xareaGreen[redIndex][greenIndex][blueIndex] = xareaGreen[redIndex - 1][greenIndex][blueIndex] + areaGreen[blueIndex];
					xareaBlue[redIndex][greenIndex][blueIndex] = xareaBlue[redIndex - 1][greenIndex][blueIndex] + areaBlue[blueIndex];
					xarea2[redIndex][greenIndex][blueIndex] = xarea2[redIndex - 1][greenIndex][blueIndex] + area2[blueIndex];

					data->Weights[alphaIndex][redIndex][greenIndex][blueIndex] = data->Weights[alphaIndex - 1][redIndex][greenIndex][blueIndex] + xarea[redIndex][greenIndex][blueIndex];
					data->MomentsAlpha[alphaIndex][redIndex][greenIndex][blueIndex] = data->MomentsAlpha[alphaIndex - 1][redIndex][greenIndex][blueIndex] + xareaAlpha[redIndex][greenIndex][blueIndex];
					data->MomentsRed[alphaIndex][redIndex][greenIndex][blueIndex] = data->MomentsRed[alphaIndex - 1][redIndex][greenIndex][blueIndex] + xareaRed[redIndex][greenIndex][blueIndex];
					data->MomentsGreen[alphaIndex][redIndex][greenIndex][blueIndex] = data->MomentsGreen[alphaIndex - 1][redIndex][greenIndex][blueIndex] + xareaGreen[redIndex][greenIndex][blueIndex];
					data->MomentsBlue[alphaIndex][redIndex][greenIndex][blueIndex] = data->MomentsBlue[alphaIndex - 1][redIndex][greenIndex][blueIndex] + xareaBlue[redIndex][greenIndex][blueIndex];
					data->Moments[alphaIndex][redIndex][greenIndex][blueIndex] = data->Moments[alphaIndex - 1][redIndex][greenIndex][blueIndex] + xarea2[redIndex][greenIndex][blueIndex];
				}
			}
		}
	}

	delete[] xarea;
	delete[] xareaAlpha;
	delete[] xareaRed;
	delete[] xareaGreen;
	delete[] xareaBlue;
	delete[] xarea2;
}


vector<Box> SplitData(int &colorCount, const _ColorData *data) {
	--colorCount;
	int next = 0;
	float volumeVariance[MaxColor] = { 0 };
	Box cubes[MaxColor] = { 0 };
	cubes[0].AlphaMaximum = MaxSideIndex;
	cubes[0].RedMaximum = MaxSideIndex;
	cubes[0].GreenMaximum = MaxSideIndex;
	cubes[0].BlueMaximum = MaxSideIndex;
	for (auto cubeIndex = 1; cubeIndex < colorCount; ++cubeIndex)
	{
		if (Cut(data, cubes[next], cubes[cubeIndex]))
		{
			volumeVariance[next] = cubes[next].Size > 1 ? CalculateVariance(data, cubes[next]) : 0.0f;
			volumeVariance[cubeIndex] = cubes[cubeIndex].Size > 1 ? CalculateVariance(data, cubes[cubeIndex]) : 0.0f;
		}
		else
		{
			volumeVariance[next] = 0.0f;
			cubeIndex--;
		}

		next = 0;
		auto temp = volumeVariance[0];

		for (auto index = 1; index <= cubeIndex; ++index)
		{
			if (volumeVariance[index] <= temp) continue;
			temp = volumeVariance[index];
			next = index;
		}

		if (temp > 0.0) continue;
		colorCount = cubeIndex + 1;
		break;
	}

	return vector<Box>(cubes, cubes + colorCount);
}

bool Cut(const _ColorData *data, Box &first, Box &second)
{
	int direction;
	auto wholeAlpha = Volume(first, data->MomentsAlpha);
	auto wholeRed = Volume(first, data->MomentsRed);
	auto wholeGreen = Volume(first, data->MomentsGreen);
	auto wholeBlue = Volume(first, data->MomentsBlue);
	auto wholeWeight = Volume(first, data->Weights);
	
	auto maxAlpha = Maximize(data, first, Alpha, (uint8_t)(first.AlphaMinimum + 1), first.AlphaMaximum, wholeAlpha, wholeRed, wholeGreen, wholeBlue, wholeWeight);
	auto maxRed = Maximize(data, first, Red, (uint8_t)(first.RedMinimum + 1), first.RedMaximum, wholeAlpha, wholeRed, wholeGreen, wholeBlue, wholeWeight);
	auto maxGreen = Maximize(data, first, Green, (uint8_t)(first.GreenMinimum + 1), first.GreenMaximum, wholeAlpha, wholeRed, wholeGreen, wholeBlue, wholeWeight);
	auto maxBlue = Maximize(data, first, Blue, (uint8_t)(first.BlueMinimum + 1), first.BlueMaximum, wholeAlpha, wholeRed, wholeGreen, wholeBlue, wholeWeight);

	if ((maxAlpha.Value >= maxRed.Value) && (maxAlpha.Value >= maxGreen.Value) && (maxAlpha.Value >= maxBlue.Value))
	{
		direction = Alpha;
		if (maxAlpha.Position == NULL) return false;
	}
	else if ((maxRed.Value >= maxAlpha.Value) && (maxRed.Value >= maxGreen.Value) && (maxRed.Value >= maxBlue.Value))
		direction = Red;
	else
	{
		if ((maxGreen.Value >= maxAlpha.Value) && (maxGreen.Value >= maxRed.Value) && (maxGreen.Value >= maxBlue.Value))
			direction = Green;
		else
			direction = Blue;
	}

	second.AlphaMaximum = first.AlphaMaximum;
	second.RedMaximum = first.RedMaximum;
	second.GreenMaximum = first.GreenMaximum;
	second.BlueMaximum = first.BlueMaximum;

	switch (direction)
	{
	case Alpha:
		second.AlphaMinimum = first.AlphaMaximum = (uint8_t)maxAlpha.Position;
		second.RedMinimum = first.RedMinimum;
		second.GreenMinimum = first.GreenMinimum;
		second.BlueMinimum = first.BlueMinimum;
		break;

	case Red:
		second.RedMinimum = first.RedMaximum = (uint8_t)maxRed.Position;
		second.AlphaMinimum = first.AlphaMinimum;
		second.GreenMinimum = first.GreenMinimum;
		second.BlueMinimum = first.BlueMinimum;
		break;

	case Green:
		second.GreenMinimum = first.GreenMaximum = (uint8_t)maxGreen.Position;
		second.AlphaMinimum = first.AlphaMinimum;
		second.RedMinimum = first.RedMinimum;
		second.BlueMinimum = first.BlueMinimum;
		break;

	case Blue:
		second.BlueMinimum = first.BlueMaximum = (uint8_t)maxBlue.Position;
		second.AlphaMinimum = first.AlphaMinimum;
		second.RedMinimum = first.RedMinimum;
		second.GreenMinimum = first.GreenMinimum;
		break;
	}

	first.Size = (first.AlphaMaximum - first.AlphaMinimum) * (first.RedMaximum - first.RedMinimum) * (first.GreenMaximum - first.GreenMinimum) * (first.BlueMaximum - first.BlueMinimum);
	second.Size = (second.AlphaMaximum - second.AlphaMinimum) * (second.RedMaximum - second.RedMinimum) * (second.GreenMaximum - second.GreenMinimum) * (second.BlueMaximum - second.BlueMinimum);

	return true;
}

CubeCut Maximize(const _ColorData *data, const Box &cube, int direction, uint8_t first, uint8_t last, int64_t wholeAlpha, int64_t wholeRed, int64_t wholeGreen, int64_t wholeBlue, int64_t wholeWeight)
{
	auto bottomAlpha = Bottom(cube, direction, data->MomentsAlpha);
	auto bottomRed = Bottom(cube, direction, data->MomentsRed);
	auto bottomGreen = Bottom(cube, direction, data->MomentsGreen);
	auto bottomBlue = Bottom(cube, direction, data->MomentsBlue);
	auto bottomWeight = Bottom(cube, direction, data->Weights);

	auto result = 0.0f;
	uint8_t cutPoint = 0;
	bool hasCutPoint = false;

	for (auto position = first; position < last; ++position)
	{
		auto halfAlpha = bottomAlpha + Top(cube, direction, position, data->MomentsAlpha);
		auto halfRed = bottomRed + Top(cube, direction, position, data->MomentsRed);
		auto halfGreen = bottomGreen + Top(cube, direction, position, data->MomentsGreen);
		auto halfBlue = bottomBlue + Top(cube, direction, position, data->MomentsBlue);
		auto halfWeight = bottomWeight + Top(cube, direction, position, data->Weights);

		if (halfWeight == 0) continue;

		auto halfDistance = halfAlpha * halfAlpha + halfRed * halfRed + halfGreen * halfGreen + halfBlue * halfBlue;
		auto temp = halfDistance / halfWeight;

		halfAlpha = wholeAlpha - halfAlpha;
		halfRed = wholeRed - halfRed;
		halfGreen = wholeGreen - halfGreen;
		halfBlue = wholeBlue - halfBlue;
		halfWeight = wholeWeight - halfWeight;

		if (halfWeight != 0)
		{
			halfDistance = halfAlpha * halfAlpha + halfRed * halfRed + halfGreen * halfGreen + halfBlue * halfBlue;
			temp += halfDistance / halfWeight;

			if (temp > result)
			{
				result = static_cast<float>(temp);
				cutPoint = position;
				hasCutPoint = true;
			}
		}
	}

	return CubeCut(cutPoint, hasCutPoint, result);
}

template<int _1, int _2, int _3>
int64_t Volume(const Box &cube, int64_t(* moment)[_1][_2][_3])
{
	return (moment[cube.AlphaMaximum][cube.RedMaximum][cube.GreenMaximum][cube.BlueMaximum] -
		moment[cube.AlphaMaximum][cube.RedMaximum][cube.GreenMinimum][cube.BlueMaximum] -
		moment[cube.AlphaMaximum][cube.RedMinimum][cube.GreenMaximum][cube.BlueMaximum] +
		moment[cube.AlphaMaximum][cube.RedMinimum][cube.GreenMinimum][cube.BlueMaximum] -
		moment[cube.AlphaMinimum][cube.RedMaximum][cube.GreenMaximum][cube.BlueMaximum] +
		moment[cube.AlphaMinimum][cube.RedMaximum][cube.GreenMinimum][cube.BlueMaximum] +
		moment[cube.AlphaMinimum][cube.RedMinimum][cube.GreenMaximum][cube.BlueMaximum] -
		moment[cube.AlphaMinimum][cube.RedMinimum][cube.GreenMinimum][cube.BlueMaximum]) -

		(moment[cube.AlphaMaximum][cube.RedMaximum][cube.GreenMaximum][cube.BlueMinimum] -
			moment[cube.AlphaMinimum][cube.RedMaximum][cube.GreenMaximum][cube.BlueMinimum] -
			moment[cube.AlphaMaximum][cube.RedMaximum][cube.GreenMinimum][cube.BlueMinimum] +
			moment[cube.AlphaMinimum][cube.RedMaximum][cube.GreenMinimum][cube.BlueMinimum] -
			moment[cube.AlphaMaximum][cube.RedMinimum][cube.GreenMaximum][cube.BlueMinimum] +
			moment[cube.AlphaMinimum][cube.RedMinimum][cube.GreenMaximum][cube.BlueMinimum] +
			moment[cube.AlphaMaximum][cube.RedMinimum][cube.GreenMinimum][cube.BlueMinimum] -
			moment[cube.AlphaMinimum][cube.RedMinimum][cube.GreenMinimum][cube.BlueMinimum]);
}

template<int _1, int _2, int _3>
float VolumeFloat(const Box &cube, float (*moment)[_1][_2][_3])
{
	return (moment[cube.AlphaMaximum][cube.RedMaximum][cube.GreenMaximum][cube.BlueMaximum] -
		moment[cube.AlphaMaximum][cube.RedMaximum][cube.GreenMinimum][cube.BlueMaximum] -
		moment[cube.AlphaMaximum][cube.RedMinimum][cube.GreenMaximum][cube.BlueMaximum] +
		moment[cube.AlphaMaximum][cube.RedMinimum][cube.GreenMinimum][cube.BlueMaximum] -
		moment[cube.AlphaMinimum][cube.RedMaximum][cube.GreenMaximum][cube.BlueMaximum] +
		moment[cube.AlphaMinimum][cube.RedMaximum][cube.GreenMinimum][cube.BlueMaximum] +
		moment[cube.AlphaMinimum][cube.RedMinimum][cube.GreenMaximum][cube.BlueMaximum] -
		moment[cube.AlphaMinimum][cube.RedMinimum][cube.GreenMinimum][cube.BlueMaximum]) -

		(moment[cube.AlphaMaximum][cube.RedMaximum][cube.GreenMaximum][cube.BlueMinimum] -
			moment[cube.AlphaMinimum][cube.RedMaximum][cube.GreenMaximum][cube.BlueMinimum] -
			moment[cube.AlphaMaximum][cube.RedMaximum][cube.GreenMinimum][cube.BlueMinimum] +
			moment[cube.AlphaMinimum][cube.RedMaximum][cube.GreenMinimum][cube.BlueMinimum] -
			moment[cube.AlphaMaximum][cube.RedMinimum][cube.GreenMaximum][cube.BlueMinimum] +
			moment[cube.AlphaMinimum][cube.RedMinimum][cube.GreenMaximum][cube.BlueMinimum] +
			moment[cube.AlphaMaximum][cube.RedMinimum][cube.GreenMinimum][cube.BlueMinimum] -
			moment[cube.AlphaMinimum][cube.RedMinimum][cube.GreenMinimum][cube.BlueMinimum]);
}

template<int _1, int _2, int _3>
int64_t Top(const Box &cube, int direction, int position, int64_t (*moment)[_1][_2][_3])
{
	switch (direction)
	{
	case Alpha:
		return (moment[position][cube.RedMaximum][cube.GreenMaximum][cube.BlueMaximum] -
			moment[position][cube.RedMaximum][cube.GreenMinimum][cube.BlueMaximum] -
			moment[position][cube.RedMinimum][cube.GreenMaximum][cube.BlueMaximum] +
			moment[position][cube.RedMinimum][cube.GreenMinimum][cube.BlueMaximum]) -
			(moment[position][cube.RedMaximum][cube.GreenMaximum][cube.BlueMinimum] -
				moment[position][cube.RedMaximum][cube.GreenMinimum][cube.BlueMinimum] -
				moment[position][cube.RedMinimum][cube.GreenMaximum][cube.BlueMinimum] +
				moment[position][cube.RedMinimum][cube.GreenMinimum][cube.BlueMinimum]);

	case Red:
		return (moment[cube.AlphaMaximum][position][cube.GreenMaximum][cube.BlueMaximum] -
			moment[cube.AlphaMaximum][position][cube.GreenMinimum][cube.BlueMaximum] -
			moment[cube.AlphaMinimum][position][cube.GreenMaximum][cube.BlueMaximum] +
			moment[cube.AlphaMinimum][position][cube.GreenMinimum][cube.BlueMaximum]) -
			(moment[cube.AlphaMaximum][position][cube.GreenMaximum][cube.BlueMinimum] -
				moment[cube.AlphaMaximum][position][cube.GreenMinimum][cube.BlueMinimum] -
				moment[cube.AlphaMinimum][position][cube.GreenMaximum][cube.BlueMinimum] +
				moment[cube.AlphaMinimum][position][cube.GreenMinimum][cube.BlueMinimum]);

	case Green:
		return (moment[cube.AlphaMaximum][cube.RedMaximum][position][cube.BlueMaximum] -
			moment[cube.AlphaMaximum][cube.RedMinimum][position][cube.BlueMaximum] -
			moment[cube.AlphaMinimum][cube.RedMaximum][position][cube.BlueMaximum] +
			moment[cube.AlphaMinimum][cube.RedMinimum][position][cube.BlueMaximum]) -
			(moment[cube.AlphaMaximum][cube.RedMaximum][position][cube.BlueMinimum] -
				moment[cube.AlphaMaximum][cube.RedMinimum][position][cube.BlueMinimum] -
				moment[cube.AlphaMinimum][cube.RedMaximum][position][cube.BlueMinimum] +
				moment[cube.AlphaMinimum][cube.RedMinimum][position][cube.BlueMinimum]);

	case Blue:
		return (moment[cube.AlphaMaximum][cube.RedMaximum][cube.GreenMaximum][position] -
			moment[cube.AlphaMaximum][cube.RedMaximum][cube.GreenMinimum][position] -
			moment[cube.AlphaMaximum][cube.RedMinimum][cube.GreenMaximum][position] +
			moment[cube.AlphaMaximum][cube.RedMinimum][cube.GreenMinimum][position]) -
			(moment[cube.AlphaMinimum][cube.RedMaximum][cube.GreenMaximum][position] -
				moment[cube.AlphaMinimum][cube.RedMaximum][cube.GreenMinimum][position] -
				moment[cube.AlphaMinimum][cube.RedMinimum][cube.GreenMaximum][position] +
				moment[cube.AlphaMinimum][cube.RedMinimum][cube.GreenMinimum][position]);

	default:
		return 0;
	}
}

template<int _1, int _2, int _3>
int64_t Bottom(const Box &cube, int direction, int64_t (*moment)[_1][_2][_3])
{
	switch (direction)
	{
	case Alpha:
		return (-moment[cube.AlphaMinimum][cube.RedMaximum][cube.GreenMaximum][cube.BlueMaximum] +
			moment[cube.AlphaMinimum][cube.RedMaximum][cube.GreenMinimum][cube.BlueMaximum] +
			moment[cube.AlphaMinimum][cube.RedMinimum][cube.GreenMaximum][cube.BlueMaximum] -
			moment[cube.AlphaMinimum][cube.RedMinimum][cube.GreenMinimum][cube.BlueMaximum]) -
			(-moment[cube.AlphaMinimum][cube.RedMaximum][cube.GreenMaximum][cube.BlueMinimum] +
				moment[cube.AlphaMinimum][cube.RedMaximum][cube.GreenMinimum][cube.BlueMinimum] +
				moment[cube.AlphaMinimum][cube.RedMinimum][cube.GreenMaximum][cube.BlueMinimum] -
				moment[cube.AlphaMinimum][cube.RedMinimum][cube.GreenMinimum][cube.BlueMinimum]);

	case Red:
		return (-moment[cube.AlphaMaximum][cube.RedMinimum][cube.GreenMaximum][cube.BlueMaximum] +
			moment[cube.AlphaMaximum][cube.RedMinimum][cube.GreenMinimum][cube.BlueMaximum] +
			moment[cube.AlphaMinimum][cube.RedMinimum][cube.GreenMaximum][cube.BlueMaximum] -
			moment[cube.AlphaMinimum][cube.RedMinimum][cube.GreenMinimum][cube.BlueMaximum]) -
			(-moment[cube.AlphaMaximum][cube.RedMinimum][cube.GreenMaximum][cube.BlueMinimum] +
				moment[cube.AlphaMaximum][cube.RedMinimum][cube.GreenMinimum][cube.BlueMinimum] +
				moment[cube.AlphaMinimum][cube.RedMinimum][cube.GreenMaximum][cube.BlueMinimum] -
				moment[cube.AlphaMinimum][cube.RedMinimum][cube.GreenMinimum][cube.BlueMinimum]);

	case Green:
		return (-moment[cube.AlphaMaximum][cube.RedMaximum][cube.GreenMinimum][cube.BlueMaximum] +
			moment[cube.AlphaMaximum][cube.RedMinimum][cube.GreenMinimum][cube.BlueMaximum] +
			moment[cube.AlphaMinimum][cube.RedMaximum][cube.GreenMinimum][cube.BlueMaximum] -
			moment[cube.AlphaMinimum][cube.RedMinimum][cube.GreenMinimum][cube.BlueMaximum]) -
			(-moment[cube.AlphaMaximum][cube.RedMaximum][cube.GreenMinimum][cube.BlueMinimum] +
				moment[cube.AlphaMaximum][cube.RedMinimum][cube.GreenMinimum][cube.BlueMinimum] +
				moment[cube.AlphaMinimum][cube.RedMaximum][cube.GreenMinimum][cube.BlueMinimum] -
				moment[cube.AlphaMinimum][cube.RedMinimum][cube.GreenMinimum][cube.BlueMinimum]);

	case Blue:
		return (-moment[cube.AlphaMaximum][cube.RedMaximum][cube.GreenMaximum][cube.BlueMinimum] +
			moment[cube.AlphaMaximum][cube.RedMaximum][cube.GreenMinimum][cube.BlueMinimum] +
			moment[cube.AlphaMaximum][cube.RedMinimum][cube.GreenMaximum][cube.BlueMinimum] -
			moment[cube.AlphaMaximum][cube.RedMinimum][cube.GreenMinimum][cube.BlueMinimum]) -
			(-moment[cube.AlphaMinimum][cube.RedMaximum][cube.GreenMaximum][cube.BlueMinimum] +
				moment[cube.AlphaMinimum][cube.RedMaximum][cube.GreenMinimum][cube.BlueMinimum] +
				moment[cube.AlphaMinimum][cube.RedMinimum][cube.GreenMaximum][cube.BlueMinimum] -
				moment[cube.AlphaMinimum][cube.RedMinimum][cube.GreenMinimum][cube.BlueMinimum]);

	default:
		return 0;
	}
}

float CalculateVariance(const _ColorData *data, const Box &cube)
{
	float volumeAlpha = static_cast<float>(Volume(cube, data->MomentsAlpha));
	float volumeRed = static_cast<float>(Volume(cube, data->MomentsRed));
	float volumeGreen = static_cast<float>(Volume(cube, data->MomentsGreen));
	float volumeBlue = static_cast<float>(Volume(cube, data->MomentsBlue));
	float volumeMoment = static_cast<float>(VolumeFloat(cube, data->Moments));
	float volumeWeight = static_cast<float>(Volume(cube, data->Weights));

	float distance = volumeAlpha * volumeAlpha + volumeRed * volumeRed + volumeGreen * volumeGreen + volumeBlue * volumeBlue;

	auto result = volumeMoment - distance / volumeWeight;

	return isnan(result) ? 0.0f : result;
}

QuantizedPalette GetQuantizedPalette(int colorCount, _ColorData *data, const vector<Box> &cubes)
{
	int imageSize = data->Pixels.size();
	auto lookups = BuildLookups(cubes, data);

	for (auto index = 0; index < imageSize; ++index)
	{
		auto indexParts = data->QuantizedPixels[index];
		data->QuantizedPixels[index] = PixelIndex(lookups.Tags
			[indexParts.PixelValue.Alpha][indexParts.PixelValue.Red][indexParts.PixelValue.Green][indexParts.PixelValue.Blue]);
	}

	auto alphas = new uint64_t[colorCount + 1]{ 0 };
	auto reds = new uint64_t[colorCount + 1]{ 0 };
	auto greens = new uint64_t[colorCount + 1]{ 0 };
	auto blues = new uint64_t[colorCount + 1]{ 0 };
	auto sums = new uint32_t[colorCount + 1]{ 0 };
	QuantizedPalette palette(imageSize);

	{
		for (int i = 0; i < imageSize; i++)
		{
			const Pixel &pixel = data->Pixels[i];
			palette.PixelIndex[i] = -1;
			if (pixel.Alpha <= AlphaThreshold)
				continue;

			auto match = data->QuantizedPixels[i];
			auto bestMatch = match.Value;
			uint32_t bestDistance = 100000000;

			for (int j = 0, j1 = lookups.Lookups.size(); j < j1; j++)
			{
				auto deltaAlpha = pixel.Alpha - lookups.Lookups[j].Alpha;
				auto deltaRed = pixel.Red - lookups.Lookups[j].Red;
				auto deltaGreen = pixel.Green - lookups.Lookups[j].Green;
				auto deltaBlue = pixel.Blue - lookups.Lookups[j].Blue;

				auto distance = deltaAlpha * deltaAlpha + deltaRed * deltaRed + deltaGreen * deltaGreen + deltaBlue * deltaBlue;

				if (distance >= bestDistance) continue;

				bestDistance = distance;
				bestMatch = j;
			}

			palette.PixelIndex[i] = bestMatch;

			alphas[bestMatch] += pixel.Alpha;
			reds[bestMatch] += pixel.Red;
			greens[bestMatch] += pixel.Green;
			blues[bestMatch] += pixel.Blue;
			sums[bestMatch]++;
		}
	}

	for (auto paletteIndex = 0; paletteIndex < colorCount; paletteIndex++)
	{
		if (sums[paletteIndex] > 0)
		{
			alphas[paletteIndex] = alphas[paletteIndex] / sums[paletteIndex];
			reds[paletteIndex] = reds[paletteIndex] / sums[paletteIndex];
			greens[paletteIndex] = greens[paletteIndex] / sums[paletteIndex];
			blues[paletteIndex] = blues[paletteIndex] / sums[paletteIndex];
		}

		auto color = Pixel(alphas[paletteIndex], reds[paletteIndex], greens[paletteIndex], blues[paletteIndex]);
		palette.Colors.push_back(color);
	}
	palette.Colors.push_back(Pixel(0, 0, 0, 0));

	delete[] alphas;
	delete[] reds;
	delete[] greens;
	delete[] blues;
	delete[] sums;

	return palette;
}

_LookupData BuildLookups(const vector<Box> &cubes, const _ColorData *data)
{
	_LookupData lookups;

	for (int i = 0, i1 = cubes.size(); i < i1; i++)
	{
		const Box& cube = cubes[i];
		for (auto alphaIndex = (uint8_t)(cube.AlphaMinimum + 1); alphaIndex <= cube.AlphaMaximum; ++alphaIndex)
		{
			for (auto redIndex = (uint8_t)(cube.RedMinimum + 1); redIndex <= cube.RedMaximum; ++redIndex)
			{
				for (auto greenIndex = (uint8_t)(cube.GreenMinimum + 1); greenIndex <= cube.GreenMaximum; ++greenIndex)
				{
					for (auto blueIndex = (uint8_t)(cube.BlueMinimum + 1); blueIndex <= cube.BlueMaximum; ++blueIndex)
						lookups.Tags[alphaIndex][redIndex][greenIndex][blueIndex] = lookups.Lookups.size();
				}
			}
		}

		auto weight = Volume(cube, data->Weights);

		if (weight <= 0) continue;

		auto lookup = Lookup();

		lookup.Alpha = (int)(Volume(cube, data->MomentsAlpha) / weight);
		lookup.Red = (int)(Volume(cube, data->MomentsRed) / weight);
		lookup.Green = (int)(Volume(cube, data->MomentsGreen) / weight);
		lookup.Blue = (int)(Volume(cube, data->MomentsBlue) / weight);
		lookups.Lookups.push_back(lookup);
	}

	return lookups;
}

void ProcessImagePixels(const BitmapData *sourceImage, const QuantizedPalette *palette, const IndexedBitmapData *destImage) {
	memcpy_s(destImage->Palette, destImage->ColorCount * sizeof(Pixel), &palette->Colors[0], palette->Colors.size() * sizeof(Pixel));

	const uint8_t targetBitDepth = 8;
	auto targetByteLength = destImage->Data.Stride < 0 ? -destImage->Data.Stride : destImage->Data.Stride;
	const int targetByteCount = max(1, targetBitDepth >> 3);
	auto targetSize = targetByteLength * destImage->Data.Height;
	auto targetOffset = 0;
	//auto targetBuffer = new byte[targetSize];
	uint8_t *targetBuffer = static_cast<uint8_t*>(destImage->Data.Scan0);
	uint8_t targetValue[targetByteCount] = { 0 };
	auto pixelIndex = 0;

	for (int y = 0, y1 = destImage->Data.Height, x1 = destImage->Data.Width; y < y1; y++)
	{
		auto targetIndex = 0;
		for (auto x = 0; x < x1; x++)
		{
			auto targetIndexOffset = targetIndex >> 3;
			targetValue[0] = (uint8_t)(palette->PixelIndex[pixelIndex] == -1 ? palette->Colors.size() - 1 : palette->PixelIndex[pixelIndex]);
			pixelIndex++;

			for (auto valueIndex = 0; valueIndex < targetByteCount; valueIndex++)
				targetBuffer[targetOffset + valueIndex + targetIndexOffset] = targetValue[valueIndex];

			targetIndex += targetBitDepth;
		}

		targetOffset += targetByteLength;
	}

	//Marshal.Copy(targetBuffer, 0, targetData.Scan0, targetSize);
}