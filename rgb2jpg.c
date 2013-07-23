/*
 * main.c
 */

#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <getopt.h>
#include <stdlib.h>
#include <math.h>

#include <jpeglib.h>
#include <jerror.h>

int rgb2jpg(const char *dst, int quality, unsigned char *src, int w, int h)
{
	int rc, y;
	FILE *outfile = NULL;
	struct jpeg_compress_struct cinfo;
	struct jpeg_error_mgr cerr;
	JSAMPROW cbuffer[1];
	int cstride;

	if (!(outfile = fopen(dst, "wb+"))) {
		rc = errno ? errno : -1;
		fprintf(stderr, "fopen: %s (%s)\n", strerror(rc), dst);
		goto finally;
	}

	cinfo.err = jpeg_std_error(&cerr);
	jpeg_create_compress(&cinfo);

	cinfo.in_color_space = JCS_RGB;
	jpeg_set_defaults(&cinfo);
	jpeg_set_quality(&cinfo, quality, TRUE);

	cinfo.image_width = w;
	cinfo.image_height = h;
	cinfo.input_components = 3;
	jpeg_stdio_dest(&cinfo, outfile);

	jpeg_start_compress(&cinfo, TRUE);

	cstride = cinfo.image_width * cinfo.input_components;

	for (y = 0; y < h; y++) {
		cbuffer[0] = src;
		jpeg_write_scanlines(&cinfo, cbuffer, 1);
		src += cstride;
	}

	jpeg_finish_compress(&cinfo);
	jpeg_destroy_compress(&cinfo);

	rc = 0;

finally:
	if (outfile) {
		fclose(outfile);
	}
	return rc;
}
