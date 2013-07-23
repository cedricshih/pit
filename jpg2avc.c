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

#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <math.h>

#include "log.h"
#include "jpg2rgb.h"
#include "resize.h"
#include "rgb2yuv.h"
#include "avcenc.h"
#include "avi.h"
#include "histogram.h"

#include "jpg2avc.h"

#define PIXEL_MIN 0
#define PIXEL_MAX 255

struct jpg2avc {
	struct avdim size;
	struct avfrac frame_rate;
	int quality;
	unsigned char *frame_buf;
	size_t frame_buf_sz;
	unsigned int ref_frame;
	struct {
		unsigned int black;
		unsigned int white;
		struct {
			double black;
			double white;
		} ratio;
	} stretch;
	struct avcenc_session *session;
	struct avi_writer *writer;
	struct histogram *histogram;
	unsigned int count;
};

static int resize(const char *infile, unsigned int w1, unsigned int h1,
		const char *outfile, unsigned int w2, unsigned int h2);
static int rgb2yuv(const char *srcfile, unsigned int w, unsigned int h,
		unsigned char *dst);
ssize_t read_file(const char *filename, void *dst, size_t maxlen);

struct jpg2avc *jpg2avc_new(struct avdim *size, struct avfrac *frame_rate,
		int quality)
{
	int rc;
	struct jpg2avc *ctx;

	if (!(ctx = calloc(1, sizeof(*ctx)))) {
		rc = errno ? errno : -1;
		error("calloc: %s", strerror(rc));
		goto finally;
	}

	ctx->frame_buf_sz = size->width * size->height * 3 / 2;

	if (!(ctx->frame_buf = malloc(ctx->frame_buf_sz))) {
		rc = errno ? errno : -1;
		error("malloc: %s", strerror(rc));
		goto finally;
	}

	memcpy(&ctx->size, size, sizeof(ctx->size));
	memcpy(&ctx->frame_rate, frame_rate, sizeof(ctx->frame_rate));
	ctx->quality = quality;

	ctx->stretch.black = 0;
	ctx->stretch.white = 255;
	ctx->stretch.ratio.black = 0;
	ctx->stretch.ratio.white = 1;

	rc = 0;

finally:
	if (rc != 0) {
		if (ctx) {
			jpg2avc_free(ctx);
		}
		ctx = NULL;
		errno = rc;
	}
	return ctx;
}

void jpg2avc_free(struct jpg2avc *ctx)
{
	if (!ctx) {
		return;
	}

	if (ctx->histogram) {
		histogram_free(ctx->histogram);
	}

	if (ctx->writer) {
		avi_writer_free(ctx->writer);
	}

	if (ctx->session) {
		avcenc_session_free(ctx->session);
	}

	if (ctx->frame_buf) {
		free(ctx->frame_buf);
	}

	free(ctx);
}

int jpg2avc_stretch_black(struct jpg2avc *ctx, unsigned int black)
{
	if (!ctx) {
		return EINVAL;
	}

	if (black > 255 || black >= ctx->stretch.white) {
		return EINVAL;
	}

	ctx->stretch.black = black;
	ctx->stretch.ratio.black = 0;

	return 0;
}

int jpg2avc_stretch_black_ratio(struct jpg2avc *ctx, double black)
{
	if (!ctx) {
		return EINVAL;
	}

	if (black < 0 || black > 1 || black >= ctx->stretch.ratio.white) {
		return EINVAL;
	}

	ctx->stretch.black = 0;
	ctx->stretch.ratio.black = black;

	return 0;
}

int jpg2avc_stretch_white(struct jpg2avc *ctx, unsigned int white)
{
	if (!ctx) {
		return EINVAL;
	}

	if (white > 255 || ctx->stretch.black >= white) {
		return EINVAL;
	}

	ctx->stretch.white = white;
	ctx->stretch.ratio.white = 1;

	return 0;
}

int jpg2avc_stretch_white_ratio(struct jpg2avc *ctx, double white)
{
	if (!ctx) {
		return EINVAL;
	}

	if (white < 0 || white > 1 || ctx->stretch.ratio.black >= white) {
		return EINVAL;
	}

	ctx->stretch.white = 255;
	ctx->stretch.ratio.white = white;

	return 0;
}

int jpg2avc_begin(struct jpg2avc *ctx, const char *output)
{
	int rc;

	if (!ctx) {
		rc = EINVAL;
		goto finally;
	}

	if (ctx->writer || ctx->session) {
		rc = EEXIST;
		goto finally;
	}

	if (!(ctx->writer = avi_writer_new(avi_fourcc('a', 'v', 'c', '1'),
			&ctx->size, &ctx->frame_rate))) {
		rc = errno ? errno : -1;
		error("avi_writer_new: %s", strerror(rc));
		goto finally;
	}

	if ((rc = avi_writer_open(ctx->writer, output))) {
		error("avi_writer_open: %s\n", strerror(rc));
		goto finally;
	}

	if (!(ctx->session = avcenc_session_new(&ctx->size, &ctx->frame_rate,
			ctx->quality))) {
		rc = errno ? errno : -1;
		error("avcenc_session_new: %s", strerror(rc));
		goto finally;
	}

	ctx->count = 0;
	rc = 0;

finally:
	if (rc != 0) {
		if (ctx->writer) {
			avi_writer_free(ctx->writer);
			ctx->writer = NULL;
		}

		if (ctx->session) {
			avcenc_session_free(ctx->session);
			ctx->session = NULL;
		}
	}
	return rc;
}

int jpg2avc_transcode(struct jpg2avc *ctx, const char *jpg, const char *rgb,
		const char *resized, const char *avc, double a, int b)
{
	int rc, i;
	struct avdim sz;
	double contrib;
	size_t n;

	if (!ctx || !jpg || !rgb || !resized || !avc) {
		return EINVAL;
	}

	if ((rc = jpg_read_header(jpg, &sz.width, &sz.height))) {
		debug("failed to read header '%s': %s", jpg, strerror(rc));
		rc = EINVAL;
		goto finally;
	}

	if (sz.width < ctx->size.width || sz.height < ctx->size.height) {
		debug("smaller resolution '%s': %dx%d\n", jpg,
				sz.width, sz.height);
		rc = EINVAL;
		goto finally;
	}

	if ((((float) ctx->size.width) / sz.width * sz.height) != ctx->size.height) {
		debug("aspect ratio mismatched '%s': %dx%d\n", jpg,
				sz.width, sz.height);
		rc = EINVAL;
		goto finally;
	}

	if ((ctx->stretch.ratio.black > 0 && ctx->stretch.black == PIXEL_MIN) ||
			(ctx->stretch.ratio.white < 1 && ctx->stretch.white == PIXEL_MAX)) {
		if (!(ctx->histogram = histogram_new(256))) {
			rc = errno ? errno : -1;
			error("histogram_new: %s", strerror(rc));
			goto finally;
		}

		if ((rc = jpg2rgb(jpg, rgb, PIXEL_MIN, PIXEL_MAX, 1.0, 0))) {
			error("failed to convert '%s': %s", jpg,
					strerror(rc));
			goto finally;
		}

		if ((rc = histogram_load(ctx->histogram, rgb, sz.width * 3, sz.height))) {
			error("failed to load histogram '%s': %s", rgb,
					strerror(rc));
			goto finally;
		}

		for (i = 0; i < histogram_size(ctx->histogram); i++) {
			contrib = histogram_contrib(ctx->histogram, i);

			if (ctx->stretch.ratio.black > 0 &&
					ctx->stretch.black == PIXEL_MIN &&
					contrib >= ctx->stretch.ratio.black) {
				ctx->stretch.black = i;
			}

			if (ctx->stretch.ratio.white < 1 &&
					ctx->stretch.white == PIXEL_MAX &&
					contrib >= ctx->stretch.ratio.white) {
				ctx->stretch.white = i;
			}
		}

		debug("stretch: %d:%d", ctx->stretch.black, ctx->stretch.white);
	}

	if ((rc = jpg2rgb(jpg, rgb, ctx->stretch.black, ctx->stretch.white, a,
			b))) {
		error("failed to convert '%s': %s", jpg, strerror(rc));
		goto finally;
	}

	if (sz.width != ctx->size.width || sz.height != ctx->size.height) {
		if ((rc = resize(rgb, sz.width, sz.height, resized,
				ctx->size.width, ctx->size.height))) {
			error("failed to resize '%s': %s", rgb, strerror(rc));
			goto finally;
		}
	} else {
		resized = rgb;
	}

	if ((rc = rgb2yuv(resized, ctx->size.width, ctx->size.height,
			ctx->frame_buf))) {
		error("failed to convert '%s': %s", resized, strerror(rc));
		goto finally;
	}

	if ((rc = avcenc_session_encode(ctx->session, ctx->frame_buf, avc))) {
		if (rc != EAGAIN) {
			error("failed to encode: %s", strerror(rc));
			goto finally;
		}
	}

	if (rc != EAGAIN) {
		if ((n = read_file(avc, ctx->frame_buf, ctx->frame_buf_sz)) < 0) {
			error("failed to read file '%s': %s", avc, strerror(rc));
			goto finally;
		}

		if ((rc = avi_writer_write(ctx->writer, ctx->frame_buf, n))) {
			error("failed to write AVI: %s", strerror(rc));
			goto finally;
		}
	}

	ctx->count++;
	rc = 0;

finally:
	if (avc) {
		unlink(avc);
	}
	if (resized) {
		unlink(resized);
	}
	if (rgb) {
		unlink(rgb);
	}
	return rc;
}

size_t jpg2avc_pending_frames(struct jpg2avc *ctx)
{
	return avcenc_session_pending_frames(ctx->session);
}

int jpg2avc_flush(struct jpg2avc *ctx, const char *avc)
{
	int rc;
	size_t n;

	if (!ctx || !avc) {
		rc = EINVAL;
		goto finally;
	}

	if (!ctx->writer || !ctx->session) {
		rc = -1;
		goto finally;
	}

	if ((rc = avcenc_session_flush(ctx->session, avc))) {
		if (rc != EAGAIN) {
			error("avcenc_session_flush: %s\n", strerror(rc));
			goto finally;
		}
	}

	if (rc != EAGAIN) {
		if ((n = read_file(avc, ctx->frame_buf, ctx->frame_buf_sz)) < 0) {
			error("failed to read file '%s': %s", avc, strerror(rc));
			goto finally;
		}

		if ((rc = avi_writer_write(ctx->writer, ctx->frame_buf, n))) {
			error("failed to write AVI: %s", strerror(rc));
			goto finally;
		}

		rc = 0;
	}

finally:
	return rc;
}

int jpg2avc_commit(struct jpg2avc *ctx)
{
	int rc;

	if (!ctx) {
		rc = EINVAL;
		goto finally;
	}

	if (!ctx->writer || !ctx->session) {
		rc = -1;
		goto finally;
	}

	if (avcenc_session_pending_frames(ctx->session) > 0) {
		rc = EINPROGRESS;
		error("has pending frames: %d", avcenc_session_pending_frames(
				ctx->session));
		goto finally;
	}

	if ((rc = avi_writer_close(ctx->writer))) {
		error("avi_writer_close: %s\n", strerror(rc));
		goto finally;
	}

	avi_writer_free(ctx->writer);
	ctx->writer = NULL;

	avcenc_session_free(ctx->session);
	ctx->session = NULL;

	if (ctx->histogram) {
		histogram_free(ctx->histogram);
		ctx->histogram = NULL;
	}

	rc = 0;

finally:
	return rc;
}

size_t jpg2avc_count(struct jpg2avc *ctx)
{
	return ctx->count;
}

int resize(const char *infile, unsigned int w1, unsigned int h1,
		const char *outfile, unsigned int w2, unsigned int h2)
{
	int rc;
	FILE *f1 = NULL, *f2 = NULL;
	struct imgsrc *src = NULL;
	struct imgdst *dst = NULL;
	int bpp1 = 3, bpp2 = 3;

	if (!(f1 = fopen(infile, "rb"))) {
		rc = errno ? errno : -1;
		error("fopen: %s", strerror(rc));
		goto finally;
	}

	if (!(f2 = fopen(outfile, "wb+"))) {
		rc = errno ? errno : -1;
		error("fopen: %s", strerror(rc));
		goto finally;
	}

	debug("resizing: %dx%d => %dx%d", w1, h2, w2, h2);

	float fy = (float) h1 / h2;
	int nrrows = (int)ceil(fy + 1.0f);
	int pos = h2 / 2 * fy;
	int t3 = pos + nrrows < h1 + 1 ? 0 : pos - h1 + 1;

	if (!(src = fiosrc_new(f1, w1, h1, bpp1, nrrows + t3))) {
		rc = errno ? errno : -1;
		error("fiosrc_new: %s", strerror(rc));
		goto finally;
	}

	if (!(dst = fiodst_new(f2, w2, h2, bpp2))) {
		rc = errno ? errno : -1;
		error("fiodst_new: %s", strerror(rc));
		goto finally;
	}

	if ((rc = scale_down(src, dst))) {
		error("scale_down: %s", strerror(rc));
		goto finally;
	}

	rc = 0;

finally:
	if (dst) {
		fiodst_free(dst);
	}
	if (src) {
		fiosrc_free(src);
	}
	if (f2) {
		fclose(f2);
	}
	if (f1) {
		fclose(f1);
	}
	return rc;
}

int rgb2yuv(const char *srcfile, unsigned int w, unsigned int h,
		unsigned char *dst)
{
	int rc;
	size_t len, n;
	unsigned char *src = NULL, *u, *v;
	FILE *file = NULL;

	len = w * h * 3;

	if ((!(src = malloc(len)))) {
		rc = errno ? errno : -1;
		error("malloc: %s", strerror(rc));
		goto finally;
	}

	if (!(file = fopen(srcfile, "rb"))) {
		rc = errno ? errno : -1;
		error("fopen: %s", strerror(rc));
		goto finally;
	}

	if ((n = fread(src, 1, len, file)) != len) {
		rc = errno ? errno : -1;
		error("fread: %s", strerror(rc));
		goto finally;
	}

	len = w * h;
	v = dst + len;
	u = v + (len >> 2);

	if ((rc = RGB2YUV(w, h, src, dst, u, v, 1))) {
		rc = errno ? errno : rc;
		error("fread: %s", strerror(rc));
		goto finally;
	}

	rc = 0;

finally:
	if (src) {
		free(src);
	}
	if (file) {
		fclose(file);
	}
	return rc;
}

ssize_t read_file(const char *filename, void *dst, size_t maxlen)
{
	int rc;
	FILE *file = NULL;
	size_t n;

	if (!(file = fopen(filename, "rb"))) {
		rc = errno ? errno : -1;
		error("fopen: %s", strerror(rc));
		goto finally;
	}

	if ((n = fread(dst, 1, maxlen, file)) < 0) {
		rc = errno ? errno : -1;
		error("fread: %s", strerror(rc));
		goto finally;
	}

	rc = 0;

finally:
	if (file) {
		fclose(file);
	}
	if (rc != 0) {
		errno = rc;
		n = -1;
	}
	return n;
}
