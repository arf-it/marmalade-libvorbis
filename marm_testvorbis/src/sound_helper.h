#include "s3e.h"

extern enum STEREO_MODE
{
	STEREO_MODE_MONO,
	STEREO_MODE_BOTH,
	STEREO_MODE_LEFT,
	STEREO_MODE_RIGHT,
	STEREO_MODE_COUNT
} g_OutputMode;

extern enum SAMPLE_RATE_CONVERTER
{
	NO_RESAMPLE,
	ZERO_ORDER_HOLD,
	FIRST_ORDER_INTERPOLATION,
	QUADRATIC_INTERPOLATION,
} conversionType;

int16 ClipToInt16(int32 sval);
int GCD(int a, int b);


