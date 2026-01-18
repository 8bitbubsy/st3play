#include <stdint.h>
#include <stdbool.h>
#include <math.h>
#include "../dig.h" // PI

// zeroth-order modified Bessel function of the first kind (series approximation)
static inline double besselI0(double z)
{
	double s = 1.0, ds = 1.0, d = 2.0;
	const double zz = z * z;

	do
	{
		ds *= zz / (d * d);
		s += ds;
		d += 2.0;
	}
	while (ds > s*(1E-12));

	return s;
}

static inline double sinc(double x)
{
	if (x == 0.0)
	{
		return 1.0;
	}
	else
	{
		x *= PI;
		return sin(x) / x;
	}
}

void makeSincKernel(float *fOut, float kaiserBeta)
{
	const double beta = kaiserBeta;

	const double besselI0Beta = 1.0 / besselI0(beta);
	for (int32_t i = 0; i < SINC_WIDTH * SINC_PHASES; i++)
	{
		const double x = ((i & (SINC_WIDTH-1)) - ((SINC_WIDTH / 2) - 1)) - ((i >> SINC_WIDTH_BITS) * (1.0 / SINC_PHASES));

		// 8bb: Kaiser-Bessel window
		const double n = x * (1.0 / (SINC_WIDTH / 2));
		const double window = besselI0(beta * sqrt(1.0 - n * n)) * besselI0Beta;

		fOut[i] = (float)(sinc(x) * window);
	}
}
