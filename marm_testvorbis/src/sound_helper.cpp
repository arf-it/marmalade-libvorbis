#include "sound_helper.h"




int GCD(int a, int b)
{
	while( 1 )
	{
		a = a % b;
		if( a == 0 )
			return b;
		b = b % a;

		if( b == 0 )
			return a;
	}
}


