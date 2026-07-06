#ifdef REC98_PORT
// REC98_PORT: see th01/hardware/graph.h's identical note -- a bare
// `#include "planar.h"` from within th01/hardware/ resolves to the sibling
// th01/hardware/planar.h, not the root one this file needs.
#include <planar.h>
#else
#include "planar.h"
#endif

// Blits the given monochrome 8×8 [sprite] to the VRAM position indicated by
// [vram_offset_topleft] and [first_bit], in the given [col] via the GRCG.
void grcg_put_8x8_mono(
	vram_offset_t vram_offset_topleft,
	char first_bit,
	const dots8_t sprite[8],
	vc2 col
);
