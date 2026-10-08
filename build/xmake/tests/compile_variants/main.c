/**************************************************************************/
/*  main.c                                                                */
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

#include <stdio.h>
#define JPEG_INTERNALS
#include "jpeglib.h"

int variant_8(void);
int variant_12(void);

int main(void) {
	JSAMPLE input8[] = { 127, 255 };
	JSAMPLE output8[] = { 0, 0 };
	JSAMPROW rows8[] = { input8 };
	JSAMPROW copied8[] = { output8 };
	J12SAMPLE input12[] = { 2048, 4095 };
	J12SAMPLE output12[] = { 0, 0 };
	J12SAMPROW rows12[] = { input12 };
	J12SAMPROW copied12[] = { output12 };
	jcopy_sample_rows(rows8, 0, copied8, 0, 1, 2);
	j12copy_sample_rows(rows12, 0, copied12, 0, 1, 2);
	if (variant_8() != 11 || variant_12() != 15 || output8[0] != 127 || output8[1] != 255 || output12[0] != 2048 || output12[1] != 4095 || jpeg_natural_order[2] != 8 || jround_up(15, 8) != 16) {
		return 1;
	}
	puts("NATIVE_COMPILE_VARIANT_RUNTIME_PASS 8");
	return 0;
}
