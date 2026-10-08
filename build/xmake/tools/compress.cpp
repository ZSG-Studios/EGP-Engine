/**************************************************************************/
/*  compress.cpp                                                          */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md). */
/* Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.                  */
/*                                                                        */
/* Permission is hereby granted, free of charge, to any person obtaining  */
/* a copy of this software and associated documentation files (the        */
/* "Software"), to deal in the Software without restriction, including    */
/* without limitation the rights to use, copy, modify, merge, publish,    */
/* distribute, sublicense, and/or sell copies of the Software, and to     */
/* permit persons to whom the Software is furnished to do so, subject to  */
/* the following conditions:                                              */
/*                                                                        */
/* The above copyright notice and this permission notice shall be         */
/* included in all copies or substantial portions of the Software.        */
/*                                                                        */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,        */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF     */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,   */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/

#include "zlib.h"

#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

int main(int argc, char **argv) {
	std::string format = "zlib", input, output;
	bool crc = false;
	for (int i = 1; i < argc; ++i) {
		const std::string option = argv[i];
		if (option == "--crc32") {
			crc = true;
		} else if (i + 1 < argc && option == "--format") {
			format = argv[++i];
		} else if (i + 1 < argc && option == "--input") {
			input = argv[++i];
		} else if (i + 1 < argc && option == "--output") {
			output = argv[++i];
		} else {
			std::fprintf(stderr, "Unknown or incomplete argument: %s\n", option.c_str());
			return 2;
		}
	}
	if (input.empty() || (!crc && output.empty()) || (format != "zlib" && format != "raw")) {
		std::fprintf(stderr, "Usage: egp_compress --input FILE [--crc32 | --output FILE --format zlib|raw]\n");
		return 2;
	}
	std::ifstream source(input, std::ios::binary);
	if (!source) {
		std::fprintf(stderr, "Cannot read input file.\n");
		return 1;
	}
	std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(source)), {});
	if (source.bad()) {
		std::fprintf(stderr, "Cannot finish reading input file.\n");
		return 1;
	}
	if (bytes.size() > 0xffffffffULL) {
		std::fprintf(stderr, "Generator input exceeds 4 GiB.\n");
		return 1;
	}
	if (crc) {
		std::printf("%08lx\n", static_cast<unsigned long>(crc32(0, bytes.data(), static_cast<uInt>(bytes.size()))));
		return 0;
	}
	z_stream stream = {};
	if (deflateInit2(&stream, Z_BEST_COMPRESSION, Z_DEFLATED, format == "raw" ? -MAX_WBITS : MAX_WBITS, 8, Z_DEFAULT_STRATEGY) != Z_OK) {
		return 1;
	}
	std::vector<unsigned char> encoded(deflateBound(&stream, static_cast<uLong>(bytes.size())));
	stream.next_in = bytes.data();
	stream.avail_in = static_cast<uInt>(bytes.size());
	stream.next_out = encoded.data();
	stream.avail_out = static_cast<uInt>(encoded.size());
	const int result = deflate(&stream, Z_FINISH);
	const auto size = stream.total_out;
	deflateEnd(&stream);
	if (result != Z_STREAM_END) {
		std::fprintf(stderr, "Compression failed: %d\n", result);
		return 1;
	}
	std::ofstream destination(output, std::ios::binary | std::ios::trunc);
	destination.write(reinterpret_cast<const char *>(encoded.data()), static_cast<std::streamsize>(size));
	destination.close();
	return destination.good() ? 0 : 1;
}
