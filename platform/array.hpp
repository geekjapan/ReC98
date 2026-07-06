// std::array polyfill
// -------------------

#if (__cplusplus >= 201103L) && !defined(REC98_PORT)
#include <array>
template <class T, size_t N> using Array = std::array<T, N>;
#else
// REC98_PORT: std::array has no .count() (that's this file's own pre-C++11
// polyfill API below), and including <array>/<algorithm> after
// compat/borland.hpp's `#define __seg` would corrupt libc++'s own `__seg`
// identifiers (see e.g. <__algorithm/for_each_n_segment.h>). Always use the
// polyfill under REC98_PORT to sidestep both problems.
template <class T, int N> struct Array {
	typedef int size_type;

private:
	T v[N];

public:
	size_type count() const {
		return N;
	}

	T* data() {
		return v;
	}

	const T* data() const {
		return v;
	}

	T& operator [](size_type pos) {
		return v[pos];
	}

	const T& operator [](size_type pos) const {
		return v[pos];
	}
};
#endif
