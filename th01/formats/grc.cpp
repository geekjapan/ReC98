#ifdef REC98_PORT
// REC98_PORT: th01/hardware/graph.h expects pc98.h's and the root planar.h's
// typedefs (screen_x_t, vram_y_t, vc2, page_t, dots16_t, ...) to already be
// visible. Most .cpp TUs get this transitively through an earlier include
// (e.g. th01/hardware/egc.h), but this file's first include is graph.h
// itself, and its quote-included "planar.h" resolves to the *sibling*
// th01/hardware/planar.h (TH01-specific macros only), not the root planar.h.
#include "planar.h"
#endif
#include "th01/hardware/graph.h"
#include "th01/formats/pf.hpp"
#include "th01/formats/grc.hpp"

grc_t grc_images[GRC_SLOT_COUNT];

int grc_load(main_grc_slot_t slot, const char fn[PF_FN_LEN])
{
	spriteformat_header_inner_t header;

	arc_file_open(fn);
	arc_file_seek(offsetof(grc_header_t, vram_w), SEEK_SET);
#ifdef REC98_PORT
	// REC98_PORT: grc_t's vram_w/h are `vram_byte_amount_t`/`pixel_t` (both
	// plain `int`, 4 bytes on a modern host), but arc_file_read_near() reads
	// sizeof(var) bytes -- and the on-disk grc_header_t stores both fields
	// as int16_t (2 bytes, matching Borland's original 16-bit `int`).
	// Reading 4 bytes per field here would consume h's on-disk bytes as
	// part of vram_w and misalign everything after. Read the on-disk width
	// explicitly instead.
	int16_t vram_w_disk, h_disk;
	arc_file_read(reinterpret_cast<uint8_t *>(&vram_w_disk), sizeof(vram_w_disk));
	arc_file_read(reinterpret_cast<uint8_t *>(&h_disk), sizeof(h_disk));
	grc_images[slot].vram_w = vram_w_disk;
	grc_images[slot].h = h_disk;
#else
	arc_file_read_near(grc_images[slot].vram_w);
	arc_file_read_near(grc_images[slot].h);
#endif

	arc_file_read_far(header);
	grc_images[slot].image_count = header.image_count;
	// MODDERS:
	/* if(header.image_count < 0 || header.image_count > GRC_IMAGES_PER_SLOT) {
		return 1;
	} */

	size_t image_size = (grc_images[slot].vram_w * grc_images[slot].h);
	arc_file_seek(sizeof(Palette4), SEEK_CUR);

	for(int image = 0; grc_images[slot].image_count > image; image++) {
		if(grc_images[slot].dots[image]) {
			delete[] grc_images[slot].dots[image];
		}
		grc_images[slot].dots[image] = new dots8_t[image_size];
		arc_file_read(grc_images[slot].dots[image], image_size);
	}
	arc_file_close();
	return 0;
}

void grc_put_8(
	screen_x_t left, vram_y_t top, main_grc_slot_t slot, int image, vc2 col
)
{
	vram_offset_t vram_offset_row = vram_offset_shift(left, top);
	vram_offset_t vram_offset;
	pixel_t grc_y;
	vram_byte_amount_t grc_x;
	size_t grc_p = 0;
	vram_y_t vram_y;
	#define grc_slot grc_images[slot]

	if(
		((grc_slot.vram_w * -BYTE_DOTS) > left) || (left >= RES_X) ||
		((grc_slot.h      *         -1) > top)  || (top  >= RES_Y)
	) {
		return;
	}

	grcg_setcolor_rmw(col);
	for(grc_y = 0; grc_slot.h > grc_y; grc_y++) {
		vram_offset = vram_offset_row;
		vram_y = (vram_offset / ROW_SIZE);
		if(vram_offset_row >= 0) { // Clip at the top edge
			for(grc_x = 0; grc_slot.vram_w > grc_x; grc_x++) {
				if(grc_slot.dots[image][grc_p]) {
					// Clip at the left and right edges
					if(
						((left >= 0) && ((vram_offset / ROW_SIZE) == vram_y)) ||
						((left <  0) && ((vram_offset / ROW_SIZE) != vram_y))
					) {
						grcg_put(vram_offset, grc_slot.dots[image][grc_p], 8);
					}
				}
				grc_p++;
				vram_offset++;
			}
		} else {
			grc_p += grc_slot.vram_w;
		}
		vram_offset_row += ROW_SIZE;
		if(vram_offset_row >= PLANE_SIZE) { // Clip at the bottom edge
			break;
		}
	}
	grcg_off_func();

	#undef grc_slot
}

void grc_free(main_grc_slot_t slot)
{
	for(int i = 0; grc_images[slot].image_count > i; i++) {
		if(grc_images[slot].dots[i]) {
			delete[] grc_images[slot].dots[i];
			grc_images[slot].dots[i] = nullptr;
		}
	}
}
