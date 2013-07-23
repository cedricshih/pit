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

int rgb2jpg(const char *dst, int quality, int black, int white, double a,
		int b, unsigned char *src, int w, int h)
{
	int rc, y, i;
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

		if (black > 0 && white < 255) {
			for (i = 0; i < cstride; i++) {
				cbuffer[0][i] = stretch(cbuffer[0][i], black, white);
			}
		}

		if (a != 1.0 || b != 0) {
			for (i = 0; i < cstride; i++) {
				if (a != 1.0) {
					cbuffer[0][i] = clamp(a *
							cbuffer[0][i]);
				}

				if (b != 0) {
					cbuffer[0][i] = clamp(b +
							cbuffer[0][i]);
				}
			}
		}

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
