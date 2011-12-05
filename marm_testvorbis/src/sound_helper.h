#include "s3e.h"

S3E_INLINE int16 ClipToInt16(int32 sval)
{
	enum
	{
		minval =  INT16_MIN,
		maxval =  INT16_MAX,
		allbits = UINT16_MAX
	};

	// quick overflow test, the addition moves valid range to 0-allbits
	if ((sval-minval) & ~allbits)
	{
		// we overflowed.
		if (sval > maxval)
			sval = maxval;
		else
			if (sval < minval)
				sval = minval;
	}

	return (int16)sval;
}

int GCD(int a, int b);


