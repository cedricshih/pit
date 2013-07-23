// $Id$
/*
 * Copyright 2013 Cedric Shih (cedric dot shih at gmail dot com)
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <string.h>
#include <stdio.h>
#include <errno.h>

#include <jpeglib.h>
#include <jerror.h>

#include "log.h"

#include "jpg2rgb.h"

static int jpgerr = 0;

static void jpg_error_exit(j_common_ptr cinfo)
{
	jpgerr = errno ? errno : -1;
}

int jpg_read_header(const char *path, size_t *w, size_t *h)
{
	int rc;
	struct jpeg_decompress_struct dinfo;
	struct jpeg_error_mgr derr;
	FILE *file = NULL;

	if (!(file = fopen(path, "rb"))) {
		rc = errno ? errno : -1;
		error("fopen: %s (%s)", strerror(rc), path);
		goto finally;
	}

	dinfo.err = jpeg_std_error(&derr);
	derr.error_exit = jpg_error_exit;

	jpeg_create_decompress(&dinfo);
	jpeg_stdio_src(&dinfo, file);

	jpgerr = 0;
	jpeg_read_header(&dinfo, TRUE);

	if (jpgerr != 0) {
		rc = jpgerr;
		goto finally;
	}

	*w = dinfo.image_width;
	*h = dinfo.image_height;

	jpeg_destroy_decompress(&dinfo);
	rc = 0;

finally:
	if (file) {
		fclose(file);
	}
	return rc;
}

static unsigned char clamp(int c)
{
	if (c < 0) {
		return 0;
	} else if (c > 255) {
		return 255;
	} else {
		return (unsigned char) c;
	}
}

static unsigned char stretch(int c, int min, int max)
{
	return clamp((c - min) * 255 / (max - min));
}

int jpg2rgb(const char *in, const char *out, int black, int white, double a,
		int b)
{
	int rc, i;
	struct jpeg_decompress_struct dinfo;
	struct jpeg_error_mgr derr;
	FILE *infile = NULL, *outfile = NULL;
	JSAMPARRAY dbuffer;
	int dstride;
	size_t n;

	if (!(infile = fopen(in, "rb"))) {
		rc = errno ? errno : -1;
		error("fopen: %s (%s)", strerror(rc), in);
		goto finally;
	}

	if (!(outfile = fopen(out, "wb+"))) {
		rc = errno ? errno : -1;
		error("fopen: %s (%s)", strerror(rc), out);
		goto finally;
	}

	dinfo.err = jpeg_std_error(&derr);
	jpeg_create_decompress(&dinfo);
	jpeg_stdio_src(&dinfo, infile);
	jpeg_read_header(&dinfo, TRUE);

	jpeg_start_decompress(&dinfo);

	dstride = dinfo.output_width * dinfo.output_components;

	if (!(dbuffer = (*dinfo.mem->alloc_sarray)((j_common_ptr) &dinfo,
			JPOOL_IMAGE, dstride, 1))) {
		rc = errno ? errno : -1;
		error("failed to allocate decoder buffer: %s",
				strerror(rc));
		goto finally;
	}

	while (dinfo.output_scanline < dinfo.output_height) {
		jpeg_read_scanlines(&dinfo, dbuffer, 1);

		if (black > 0 && white < 255) {
			for (i = 0; i < dstride; i++) {
				dbuffer[0][i] = stretch(dbuffer[0][i], black, white);
			}
		}

		if (a != 1.0 || b != 0) {
			for (i = 0; i < dstride; i++) {
				if (a != 1.0) {
					dbuffer[0][i] = clamp(a *
							dbuffer[0][i]);
				}

				if (b != 0) {
					dbuffer[0][i] = clamp(b +
							dbuffer[0][i]);
				}
			}
		}

		if ((n = fwrite(dbuffer[0], dinfo.output_components,
				dinfo.output_width, outfile)) != dinfo.output_width) {
			rc = errno ? errno : -1;
			error("fwrite: %s (%s)", strerror(rc), out);
			goto finally;
		}
	}

	jpeg_finish_decompress(&dinfo);
	jpeg_destroy_decompress(&dinfo);

	rc = 0;

finally:
	if (infile) {
		fclose(infile);
	}
	if (outfile) {
		fclose(outfile);
	}
	return rc;
}
