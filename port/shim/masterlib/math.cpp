// port/shim/masterlib/math
// -------------------------
// libs/master.lib/master.hpp's fixed-point trig table (SinTable8/CosTable8,
// used via the Sin8()/Cos8() macros) and integer math helpers
// (iatan2/isqrt/ihypot), needed by th01/math/polar.hpp and various boss/
// bullet trajectory code in th01/main/** -- unreached by the OP.EXE title/
// menu closure (T9), so T3's masterlib shim never had to implement them.
//
// Table format, derived from usage (th01/math/polar.hpp's polar_x()/
// polar_y(): `(radius * Cos8(angle)) >> 8`): 256-entry Q8 fixed-point
// sine/cosine (amplitude 256, matching the >>8 divide), angle 0..255
// mapping linearly to 0..2*pi. Values generated from libm sin()/cos(),
// rounded to the nearest integer -- not necessarily bit-identical to
// master.lib's real table, but mathematically equivalent, which is all
// gameplay code (movement/aiming trajectories) actually depends on.
// iatan2()/isqrt()/ihypot() are plain integer wrappers around libm in the
// same angle/value domain.
#include "port/compat/borland.hpp"

#include <cmath>
#include <cstdlib>

#include "libs/master.lib/master.hpp"

const short SinTable8[256] = {
	0, 6, 13, 19, 25, 31, 38, 44, 50, 56, 62, 68, 74, 80, 86, 92,
	98, 104, 109, 115, 121, 126, 132, 137, 142, 147, 152, 157, 162, 167, 172, 177,
	181, 185, 190, 194, 198, 202, 206, 209, 213, 216, 220, 223, 226, 229, 231, 234,
	237, 239, 241, 243, 245, 247, 248, 250, 251, 252, 253, 254, 255, 255, 256, 256,
	256, 256, 256, 255, 255, 254, 253, 252, 251, 250, 248, 247, 245, 243, 241, 239,
	237, 234, 231, 229, 226, 223, 220, 216, 213, 209, 206, 202, 198, 194, 190, 185,
	181, 177, 172, 167, 162, 157, 152, 147, 142, 137, 132, 126, 121, 115, 109, 104,
	98, 92, 86, 80, 74, 68, 62, 56, 50, 44, 38, 31, 25, 19, 13, 6,
	0, -6, -13, -19, -25, -31, -38, -44, -50, -56, -62, -68, -74, -80, -86, -92,
	-98, -104, -109, -115, -121, -126, -132, -137, -142, -147, -152, -157, -162, -167, -172, -177,
	-181, -185, -190, -194, -198, -202, -206, -209, -213, -216, -220, -223, -226, -229, -231, -234,
	-237, -239, -241, -243, -245, -247, -248, -250, -251, -252, -253, -254, -255, -255, -256, -256,
	-256, -256, -256, -255, -255, -254, -253, -252, -251, -250, -248, -247, -245, -243, -241, -239,
	-237, -234, -231, -229, -226, -223, -220, -216, -213, -209, -206, -202, -198, -194, -190, -185,
	-181, -177, -172, -167, -162, -157, -152, -147, -142, -137, -132, -126, -121, -115, -109, -104,
	-98, -92, -86, -80, -74, -68, -62, -56, -50, -44, -38, -31, -25, -19, -13, -6,
};

const short CosTable8[256] = {
	256, 256, 256, 255, 255, 254, 253, 252, 251, 250, 248, 247, 245, 243, 241, 239,
	237, 234, 231, 229, 226, 223, 220, 216, 213, 209, 206, 202, 198, 194, 190, 185,
	181, 177, 172, 167, 162, 157, 152, 147, 142, 137, 132, 126, 121, 115, 109, 104,
	98, 92, 86, 80, 74, 68, 62, 56, 50, 44, 38, 31, 25, 19, 13, 6,
	0, -6, -13, -19, -25, -31, -38, -44, -50, -56, -62, -68, -74, -80, -86, -92,
	-98, -104, -109, -115, -121, -126, -132, -137, -142, -147, -152, -157, -162, -167, -172, -177,
	-181, -185, -190, -194, -198, -202, -206, -209, -213, -216, -220, -223, -226, -229, -231, -234,
	-237, -239, -241, -243, -245, -247, -248, -250, -251, -252, -253, -254, -255, -255, -256, -256,
	-256, -256, -256, -255, -255, -254, -253, -252, -251, -250, -248, -247, -245, -243, -241, -239,
	-237, -234, -231, -229, -226, -223, -220, -216, -213, -209, -206, -202, -198, -194, -190, -185,
	-181, -177, -172, -167, -162, -157, -152, -147, -142, -137, -132, -126, -121, -115, -109, -104,
	-98, -92, -86, -80, -74, -68, -62, -56, -50, -44, -38, -31, -25, -19, -13, -6,
	0, 6, 13, 19, 25, 31, 38, 44, 50, 56, 62, 68, 74, 80, 86, 92,
	98, 104, 109, 115, 121, 126, 132, 137, 142, 147, 152, 157, 162, 167, 172, 177,
	181, 185, 190, 194, 198, 202, 206, 209, 213, 216, 220, 223, 226, 229, 231, 234,
	237, 239, 241, 243, 245, 247, 248, 250, 251, 252, 253, 254, 255, 255, 256, 256,
};

int iatan2(int y, int x) {
	double angle = std::atan2(static_cast<double>(y), static_cast<double>(x));
	int t = static_cast<int>(std::lround(angle * 256.0 / (2.0 * M_PI)));
	return (t & 0xFF);
}

int isqrt(long x) {
	if(x <= 0) {
		return 0;
	}
	return static_cast<int>(std::lround(std::sqrt(static_cast<double>(x))));
}

int ihypot(int x, int y) {
	return static_cast<int>(std::lround(
		std::hypot(static_cast<double>(x), static_cast<double>(y))
	));
}
