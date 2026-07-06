#ifdef REC98_PORT
// REC98_PORT: see th01/hardware/graph.h's identical note -- a bare
// `#include "planar.h"` from within th01/hardware/ resolves to the sibling
// th01/hardware/planar.h, not the root one this file needs.
#include <planar.h>
#else
#include "planar.h"
#endif

// Recovers the dots set in the given [mask] from (⌊left/8⌋*8, top) to
// (⌊(left/8) + w⌋*8, top) from VRAM page 1.
void graph_hline_unput_masked_8(
	screen_x_t left, vram_y_t top, dots8_t *mask, vram_byte_amount_t vram_w
);
