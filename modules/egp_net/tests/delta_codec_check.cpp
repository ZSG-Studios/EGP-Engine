/**************************************************************************/
/*  delta_codec_check.cpp                                                 */
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

// Include the implementation in this test-only TU to exercise the private codec.
#include "../net_core.cpp"

#include <cstdlib>
#include <iostream>
using namespace egp::net;
static void check(bool ok, const char *message) {
	if (!ok) {
		std::cerr << message << "\n";
		std::exit(1);
	}
}
int main() {
	std::vector<uint8_t> base(256, 3), state = base, out;
	state[12] = 9;
	state[200] = 8;
	auto patch = state_delta(base, state);
	check(!patch.empty() && patch.size() + 8 < state.size(), "sparse codec beneficial");
	check(restore_delta(base, patch, out) && out == state, "sparse restore exact");
	auto bad = patch;
	bad.push_back(0);
	check(!restore_delta(base, bad, out), "trailing patch rejected");
	bad = patch;
	bad.pop_back();
	check(!restore_delta(base, bad, out), "truncated patch rejected");
	bad = patch;
	bad[2] = 255;
	bad[3] = 255;
	check(!restore_delta(base, bad, out), "unbounded run count rejected");
	bad = patch;
	bad[4] = 255;
	bad[5] = 255;
	check(!restore_delta(base, bad, out), "out of range offset rejected");
	bad = patch;
	bad[6] = 0;
	bad[7] = 0;
	check(!restore_delta(base, bad, out), "empty run rejected");
	bad = patch;
	bad[9] = 1;
	bad[10] = 0;
	check(!restore_delta(base, bad, out), "overlapping or descending run rejected");
	bad = patch;
	bad[0] = 255;
	check(!restore_delta(base, bad, out), "wrong baseline size rejected");
	check(state_delta(base, std::vector<uint8_t>(257)).empty(), "resizing state requires full baseline");
	check(state_delta(base, std::vector<uint8_t>(256, 4)).empty(), "dense patch full fallback");
	check(state_delta(base, base).empty(), "unchanged state no delta");
	std::cout << "ACK_DELTA_CODEC_CHECKS_PASS 12\n";
}
