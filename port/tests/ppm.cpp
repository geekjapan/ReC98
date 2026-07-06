#include "tests/ppm.hpp"

#include <cctype>
#include <cstdio>
#include <cstdlib>

namespace ppm {

Image from_rgba8888(int w, int h, const uint32_t *rgba) {
	Image img;
	img.w = w;
	img.h = h;
	img.rgb.resize(static_cast<size_t>(w) * h * 3);
	for(int i = 0; i < w * h; i++) {
		uint32_t px = rgba[i];
		img.rgb[i * 3 + 0] = static_cast<uint8_t>((px >> 24) & 0xFF);
		img.rgb[i * 3 + 1] = static_cast<uint8_t>((px >> 16) & 0xFF);
		img.rgb[i * 3 + 2] = static_cast<uint8_t>((px >> 8) & 0xFF);
	}
	return img;
}

bool write(const std::string &path, const Image &img, std::string *err) {
	if(img.rgb.size() != static_cast<size_t>(img.w) * img.h * 3) {
		if(err) *err = "'" + path + "': image dimensions don't match pixel buffer size";
		return false;
	}

	std::FILE *f = std::fopen(path.c_str(), "wb");
	if(!f) {
		if(err) *err = "failed to open '" + path + "' for writing";
		return false;
	}

	std::fprintf(f, "P6\n%d %d\n255\n", img.w, img.h);
	size_t written = std::fwrite(img.rgb.data(), 1, img.rgb.size(), f);
	std::fclose(f);

	if(written != img.rgb.size()) {
		if(err) *err = "short write to '" + path + "'";
		return false;
	}
	return true;
}

namespace {

// Reads one whitespace-delimited header token, skipping leading whitespace
// and '#'-prefixed comment lines. Returns false on EOF before a token starts.
bool read_header_token(std::FILE *f, std::string *tok) {
	tok->clear();
	int c;
	for(;;) {
		c = std::fgetc(f);
		if(c == EOF) return false;
		if(c == '#') {
			while(c != '\n' && c != EOF) c = std::fgetc(f);
			continue;
		}
		if(!std::isspace(static_cast<unsigned char>(c))) break;
	}
	while(c != EOF && !std::isspace(static_cast<unsigned char>(c))) {
		tok->push_back(static_cast<char>(c));
		c = std::fgetc(f);
	}
	return !tok->empty();
}

} // namespace

bool read(const std::string &path, Image *out, std::string *err) {
	std::FILE *f = std::fopen(path.c_str(), "rb");
	if(!f) {
		if(err) *err = "failed to open '" + path + "' for reading";
		return false;
	}

	std::string magic, ws, hs, maxval;
	if(!read_header_token(f, &magic) || magic != "P6" ||
	   !read_header_token(f, &ws) ||
	   !read_header_token(f, &hs) ||
	   !read_header_token(f, &maxval)) {
		if(err) *err = "'" + path + "': malformed PPM header";
		std::fclose(f);
		return false;
	}
	if(maxval != "255") {
		if(err) *err = "'" + path + "': unsupported maxval (only 255 is supported)";
		std::fclose(f);
		return false;
	}
	// read_header_token()'s tokenizing loop already consumed the single
	// whitespace byte that separates maxval from the raw pixel data (it
	// reads one char past the token to detect the boundary) -- the file
	// position is already at the first pixel byte.

	int w = std::atoi(ws.c_str());
	int h = std::atoi(hs.c_str());
	if(w <= 0 || h <= 0) {
		if(err) *err = "'" + path + "': invalid dimensions";
		std::fclose(f);
		return false;
	}

	out->w = w;
	out->h = h;
	out->rgb.assign(static_cast<size_t>(w) * h * 3, 0);
	size_t got = std::fread(out->rgb.data(), 1, out->rgb.size(), f);
	std::fclose(f);

	if(got != out->rgb.size()) {
		if(err) *err = "'" + path + "': truncated pixel data";
		return false;
	}
	return true;
}

bool compare(const Image &a, const Image &b, int tolerance, Diff *diff) {
	if(a.w != b.w || a.h != b.h) {
		if(diff) {
			diff->reason = "size mismatch: " + std::to_string(a.w) + "x" + std::to_string(a.h) +
				" vs " + std::to_string(b.w) + "x" + std::to_string(b.h);
		}
		return false;
	}

	for(size_t i = 0; i < a.rgb.size(); i++) {
		int d = static_cast<int>(a.rgb[i]) - static_cast<int>(b.rgb[i]);
		if(d < 0) d = -d;
		if(d > tolerance) {
			if(diff) {
				size_t pixel = i / 3;
				diff->x = static_cast<int>(pixel % static_cast<size_t>(a.w));
				diff->y = static_cast<int>(pixel / static_cast<size_t>(a.w));
				diff->channel = static_cast<int>(i % 3);
				diff->a = a.rgb[i];
				diff->b = b.rgb[i];
				diff->reason = "channel value mismatch";
			}
			return false;
		}
	}
	return true;
}

} // namespace ppm
