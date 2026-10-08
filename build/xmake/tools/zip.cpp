/**************************************************************************/
/*  zip.cpp                                                               */
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

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

static void word(std::ostream &stream, uint32_t value, unsigned size) {
	for (unsigned i = 0; i < size; ++i) {
		stream.put(static_cast<char>((value >> (i * 8)) & 255));
	}
}

struct Entry {
	std::string name;
	uint32_t crc, compressed, size, offset, attributes;
};

int main(int argc, char **argv) {
	std::string root, output;
	for (int i = 1; i < argc; ++i) {
		const std::string option = argv[i];
		if (i + 1 < argc && option == "--root") {
			root = argv[++i];
		} else if (i + 1 < argc && option == "--output") {
			output = argv[++i];
		} else {
			return 2;
		}
	}
	if (root.empty() || output.empty()) {
		std::fprintf(stderr, "Usage: egp_zip --root DIRECTORY --output FILE.zip\n");
		return 2;
	}
	try {
		const auto directory = std::filesystem::canonical(root);
		const auto archive = std::filesystem::absolute(output).lexically_normal();
		std::vector<std::filesystem::path> files;
		for (const auto &entry : std::filesystem::recursive_directory_iterator(directory)) {
			if (entry.is_regular_file() && !entry.is_symlink() && entry.path() != archive) {
				files.push_back(entry.path());
			}
		}
		std::sort(files.begin(), files.end());
		if (files.size() > 65535) {
			throw std::runtime_error("ZIP file count exceeds classic ZIP limit");
		}
		std::ofstream destination(archive, std::ios::binary | std::ios::trunc);
		if (!destination) {
			throw std::runtime_error("Cannot create ZIP output");
		}
		std::vector<Entry> entries;
		for (const auto &file : files) {
			const auto name = std::filesystem::relative(file, directory).generic_u8string();
			if (name.size() > 65535 || std::filesystem::file_size(file) > 0xffffffffULL || destination.tellp() > 0xffffffffULL) {
				throw std::runtime_error("ZIP entry exceeds classic ZIP limit");
			}
			std::ifstream source(file, std::ios::binary);
			if (!source) {
				throw std::runtime_error("Cannot read ZIP input");
			}
			std::vector<unsigned char> raw((std::istreambuf_iterator<char>(source)), {});
			if (source.bad()) {
				throw std::runtime_error("Cannot finish reading ZIP input");
			}
			z_stream stream = {};
			if (deflateInit2(&stream, 9, Z_DEFLATED, -MAX_WBITS, 8, Z_DEFAULT_STRATEGY) != Z_OK) {
				throw std::runtime_error("ZIP deflate initialization failed");
			}
			std::vector<unsigned char> encoded(deflateBound(&stream, static_cast<uLong>(raw.size())));
			stream.next_in = raw.data();
			stream.avail_in = static_cast<uInt>(raw.size());
			stream.next_out = encoded.data();
			stream.avail_out = static_cast<uInt>(encoded.size());
			const int result = deflate(&stream, Z_FINISH);
			const auto length = stream.total_out;
			deflateEnd(&stream);
			if (result != Z_STREAM_END) {
				throw std::runtime_error("ZIP deflate failed");
			}
			const auto permissions = static_cast<uint32_t>(std::filesystem::status(file).permissions()) & 0777;
			const Entry entry = { name, static_cast<uint32_t>(crc32(0, raw.data(), static_cast<uInt>(raw.size()))), static_cast<uint32_t>(length), static_cast<uint32_t>(raw.size()), static_cast<uint32_t>(destination.tellp()), (0100000 | permissions) << 16 };
			word(destination, 0x04034b50, 4);
			word(destination, 20, 2);
			word(destination, 0x0800, 2);
			word(destination, 8, 2);
			word(destination, 0, 2);
			word(destination, 0x21, 2);
			word(destination, entry.crc, 4);
			word(destination, entry.compressed, 4);
			word(destination, entry.size, 4);
			word(destination, static_cast<uint32_t>(name.size()), 2);
			word(destination, 0, 2);
			destination.write(name.data(), static_cast<std::streamsize>(name.size()));
			destination.write(reinterpret_cast<const char *>(encoded.data()), static_cast<std::streamsize>(length));
			entries.push_back(entry);
		}
		if (static_cast<uint64_t>(destination.tellp()) > 0xffffffffULL) {
			throw std::runtime_error("ZIP output exceeds classic ZIP size limit");
		}
		const auto central_offset = static_cast<uint32_t>(destination.tellp());
		for (const auto &entry : entries) {
			word(destination, 0x02014b50, 4);
			word(destination, 0x0314, 2);
			word(destination, 20, 2);
			word(destination, 0x0800, 2);
			word(destination, 8, 2);
			word(destination, 0, 2);
			word(destination, 0x21, 2);
			word(destination, entry.crc, 4);
			word(destination, entry.compressed, 4);
			word(destination, entry.size, 4);
			word(destination, static_cast<uint32_t>(entry.name.size()), 2);
			word(destination, 0, 2);
			word(destination, 0, 2);
			word(destination, 0, 2);
			word(destination, 0, 2);
			word(destination, entry.attributes, 4);
			word(destination, entry.offset, 4);
			destination.write(entry.name.data(), static_cast<std::streamsize>(entry.name.size()));
		}
		if (static_cast<uint64_t>(destination.tellp()) > 0xffffffffULL) {
			throw std::runtime_error("ZIP central directory exceeds classic ZIP size limit");
		}
		const auto central_size = static_cast<uint32_t>(destination.tellp()) - central_offset;
		word(destination, 0x06054b50, 4);
		word(destination, 0, 2);
		word(destination, 0, 2);
		word(destination, static_cast<uint32_t>(entries.size()), 2);
		word(destination, static_cast<uint32_t>(entries.size()), 2);
		word(destination, central_size, 4);
		word(destination, central_offset, 4);
		word(destination, 0, 2);
		destination.close();
		return destination.good() ? 0 : 1;
	} catch (const std::exception &error) {
		std::fprintf(stderr, "ZIP packaging failed: %s\n", error.what());
		return 1;
	}
}
