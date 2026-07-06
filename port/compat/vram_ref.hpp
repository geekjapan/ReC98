// port/compat
// -----------
// VramRef<T>: the REC98_PORT stand-in for planar.h's VRAM_CHUNK() macro.
//
// On real PC-98 hardware, the GRCG intercepts CPU writes to the VRAM
// segments regardless of which single plane's segment the `mov` targeted
// (RMW/TDW mode fan the write out to every non-excluded plane). So a plain
// `*(dots8_t far *)(VRAM_PLANE_B + offset) = x` is not actually a "plain
// write" on real hardware whenever the GRCG is armed -- it has to go
// through the same GRCG state machine as the dedicated grcg_put() path.
// VramRef captures that: reads/writes route through pc98emu's
// grcg_vram_read()/grcg_vram_write(), so VRAM_CHUNK-based code and
// grcg_put()-based code observe identical, GRCG-aware behavior.

#ifndef PORT_COMPAT_VRAM_REF_HPP
#define PORT_COMPAT_VRAM_REF_HPP

#if !defined(REC98_PORT)
#error compat/vram_ref.hpp is only meant to be included in REC98_PORT builds
#endif

#include <cstdint>

#include "pc98emu/grcg.hpp"

// Deliberately global-namespace (not port::) -- planar.h's VRAM_CHUNK macro
// expands to `VramRef<...>(...)` unqualified, exactly as it would name a
// type visible at the point of use in ordinary ReC98 game code.
template <class T> struct VramRef {
	unsigned plane;
	uint16_t offset;

	VramRef(unsigned plane_, uint16_t offset_) : plane(plane_), offset(offset_) {}

	operator T() const {
		return static_cast<T>(grcg_vram_read(plane, offset, sizeof(T) * 8));
	}

	VramRef &operator=(T value) {
		grcg_vram_write(plane, offset, static_cast<uint32_t>(value), sizeof(T) * 8);
		return *this;
	}

	VramRef &operator=(const VramRef &other) {
		return (*this = static_cast<T>(other));
	}

	VramRef &operator|=(T value) {
		return (*this = static_cast<T>(static_cast<T>(*this) | value));
	}

	VramRef &operator&=(T value) {
		return (*this = static_cast<T>(static_cast<T>(*this) & value));
	}

	VramRef &operator^=(T value) {
		return (*this = static_cast<T>(static_cast<T>(*this) ^ value));
	}
};

#endif // PORT_COMPAT_VRAM_REF_HPP
