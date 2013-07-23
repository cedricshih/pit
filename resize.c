#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

#include "log.h"

#include "resize.h"

double ceil(double x);

#define CALLOC(ptr, type, n) do { \
	if (!(ptr = calloc(n, sizeof(type)))) { \
		rc = errno ? errno : -1; \
		error("calloc: %s", strerror(rc)); \
		goto finally; \
	} \
} while (0)

#define MALLOC(ptr, type, n) do { \
	if (!(ptr = malloc(sizeof(type) * n))) { \
		rc = errno ? errno : -1; \
		error("malloc: %s", strerror(rc)); \
		goto finally; \
	} \
} while (0)

#define FREE(ptr) do { \
	if (ptr) { \
		free(ptr); \
	} \
} while (0)

struct imgsrc;

struct imgdst;

typedef unsigned char *(*read_scanline_fn)(struct imgsrc *img, int row);

typedef int (*write_scanline_fn)(struct imgdst *img, unsigned char *scanline);

struct imgsrc {
	int width;
	int height;
	int bpp;
	int rowsize;
	void *arg;
	read_scanline_fn read_scanline;
};

struct imgdst {
	int width;
	int height;
	int bpp;
	int rowsize;
	void *arg;
	int row;
	write_scanline_fn write_scanline;
};

static int rowstride(int bpp, int width)
{
	int rowsize = bpp * width;
	return rowsize + ((rowsize % 4 > 0) ? 4 - (rowsize % 4) : 0);
}

static unsigned char *memsrc_read_scanline(struct imgsrc *img, int row)
{
//	debug("scanline: %d", row);
	return ((unsigned char *) img->arg) + row * img->rowsize;
}

struct imgsrc *memsrc_new(unsigned char *data, int width, int height, int bpp)
{
	int rc;
	struct imgsrc *img;

	CALLOC(img, *img, 1);

	img->width = width;
	img->height = height;
	img->bpp = bpp;
	img->rowsize = rowstride(bpp, width);
	img->arg = data;
	img->read_scanline = memsrc_read_scanline;

	rc = 0;

finally:
	if (rc != 0) {
		if (img) {
			free(img);
		}
		errno = rc;
	}
	return img;
}

void memsrc_free(struct imgsrc *img)
{
	if (!img) {
		return;
	}

	free(img);
}

static int memdst_write_scanline(struct imgdst *img, unsigned char *scanline)
{
	if (img->row >= img->height) {
		error("all scanlines written");
		return -1;
	}

	memcpy(((unsigned char *) img->arg) + (img->row++) * img->rowsize,
			scanline, img->rowsize);
	return 0;
}

struct imgdst *memdst_new(unsigned char *data, int width, int height, int bpp)
{
	int rc;
	struct imgdst *img;

	CALLOC(img, *img, 1);

	img->width = width;
	img->height = height;
	img->bpp = bpp;
	img->rowsize = rowstride(bpp, width);
	img->arg = data;
	img->write_scanline = memdst_write_scanline;

	rc = 0;

finally:
	if (rc != 0) {
		if (img) {
			free(img);
		}
		errno = rc;
	}
	return img;
}

void memdst_free(struct imgdst *img)
{
	if (!img) {
		return;
	}

	free(img);
}

struct fiosrc {
	FILE *file;
	unsigned char *row_cache;
	unsigned char **row_caches;
	int num_caches;
	int first_row;
};

static void fiosrc_map_cache(struct fiosrc *src, int rowsize)
{
	int i, j;
	unsigned char *row = src->row_cache;

//	debug("mapping caches: %d", src->first_row);

	for (i = 0; i < src->num_caches; i++) {
		j = (i + src->first_row) % src->num_caches;

//		debug("mapping cache: %d (%d) => %d", src->first_row + i,
//				i, j);

		src->row_caches[j] = row;
		row += rowsize;
	}
}

static int fiosrc_read_row(struct fiosrc *src, int rowsize)
{
	int rc;
	int next_row = src->first_row + src->num_caches;
	int i = next_row % src->num_caches;

//	debug("reading next row: %d => %d", next_row, i);

	if (fread(src->row_cache + (i * rowsize), 1, rowsize, src->file) !=
			rowsize) {
		rc = errno ? errno : -1;
		error("fread: %s", strerror(rc));
		goto finally;
	}

	src->first_row++;
	fiosrc_map_cache(src, rowsize);
	rc = 0;

finally:
	return rc;
}

static unsigned char *fiosrc_read_scanline(struct imgsrc *img, int row)
{
	int rc;
	struct fiosrc *src = img->arg;
	int next_row = src->first_row + src->num_caches;

//	debug("reading row: %d", row);

	if (src->first_row < 0) {
//		debug("caching first %d rows", src->num_caches);

		if (fread(src->row_cache, img->rowsize, src->num_caches, src->file) !=
				src->num_caches) {
			rc = errno ? errno : -1;
			error("fread: %s", strerror(rc));
			return NULL;
		}

		src->first_row = 0;
		fiosrc_map_cache(src, img->rowsize);
	} else if (row == next_row) {
		fiosrc_read_row(src, img->rowsize);
		fiosrc_map_cache(src, img->rowsize);
	} else if (row > next_row) {
		error("not incremental read");
		return NULL;
	}

	return src->row_caches[row - src->first_row];
}

struct imgsrc *fiosrc_new(FILE *file, int width, int height, int bpp,
		int row_caches)
{
	int rc;
	struct imgsrc *img = NULL;
	struct fiosrc *src = NULL;

	CALLOC(img, *img, 1);

	img->width = width;
	img->height = height;
	img->bpp = bpp;
	img->rowsize = rowstride(bpp, width);
	img->read_scanline = fiosrc_read_scanline;

	CALLOC(src, *src, 1);
	src->file = file;
	src->first_row = -1;
	src->num_caches = row_caches;
	CALLOC(src->row_caches, unsigned char *, row_caches);

	if (!(src->row_cache = malloc(row_caches * img->rowsize))) {
		rc = errno ? errno : -1;
		goto finally;
	}

	img->arg = src;
	rc = 0;

finally:
	if (rc != 0) {
		if (src) {
			FREE(src->row_caches);
			FREE(src->row_cache);
			free(src);
		}

		if (img) {
			free(img);
		}

		errno = rc;
	}
	return img;
}

void fiosrc_free(struct imgsrc *img)
{
	struct fiosrc *src;

	if (!img) {
		return;
	}

	src = img->arg;

	if (src) {
		FREE(src->row_caches);
		FREE(src->row_cache);
		free(src);
	}

	free(img);
}

static int fiodst_write_scanline(struct imgdst *img, unsigned char *scanline)
{
	if (img->row >= img->height) {
		error("all scanlines written");
		return -1;
	}

	if (fwrite(scanline, 1, img->rowsize, img->arg) != img->rowsize) {
		return errno ? errno : -1;
	}

	img->row++;
	return 0;
}

struct imgdst *fiodst_new(FILE *file, int width, int height, int bpp)
{
	int rc;
	struct imgdst *img;

	CALLOC(img, *img, 1);

	img->width = width;
	img->height = height;
	img->bpp = bpp;
	img->rowsize = rowstride(bpp, width);
	img->arg = file;
	img->write_scanline = fiodst_write_scanline;

	rc = 0;

finally:
	if (rc != 0) {
		if (img) {
			free(img);
		}
		errno = rc;
	}
	return img;
}

void fiodst_free(struct imgdst *img)
{
	if (!img) {
		return;
	}

	free(img);
}

#define FREE_ARRAY(ptr, n) do { \
	if (ptr) { \
		int i; \
		for (i = 0; i < n; i++) { \
			if (ptr[i]) { \
				free(ptr[i]); \
			} \
		} \
		free(ptr); \
	} \
} while (0)

#define MAX(a, b) a > b ? a : b;
#define MIN(a, b) a > b ? b : a;
#define GAMMASIZE 200000
#define LANCZOS_WINDOW 2
#define LANCZOS_BLUR 1.25
#define CAP(x) ((x) < 0.0f ? 0.0f : (x) > 1.0f ? 1.0f : (x))

static float *togamma = NULL;
static unsigned char *fromgamma = NULL;

static void init_gamma(void)
{
	int i1, i2, i3;

	if (togamma != NULL && fromgamma != NULL)
		return;

	togamma = malloc(sizeof(float) * 256);

	for (i1 = 0; i1 < 256; i1++)
		togamma[i1] = (float)pow(i1 / 255.0, 2.2);

	fromgamma = malloc(sizeof(unsigned char) * (GAMMASIZE + 50000));

    for (i2 = 0; i2 < GAMMASIZE + 1; i2++)
		fromgamma[i2] = (unsigned char)(pow((double)i2 / GAMMASIZE, 1 / 2.2) * 255);

	// In case of overflow
	for (i3 = GAMMASIZE + 1; i3 < GAMMASIZE + 50000; i3++)
		fromgamma[i3] = 255;
}

static float lanczos(float x)
{
	if (x == 0.0f)
		return 1.0f;
	else
	{
		float xpi = x * 3.141593f;

		return LANCZOS_WINDOW * sin(xpi) * sin(xpi / LANCZOS_WINDOW) / (xpi * xpi);
	}
}

int scale_up(unsigned char *buffer, int width, int height, int bpp, int rowsize,
		struct imgdst *dst)
{
	int rc;
	float fx = (float) width / dst->width;
	float fy = (float) height / dst->height;
	int *startx = NULL, *endx = NULL, *starty = NULL, *endy = NULL;
	float **hweights = NULL, **vweights = NULL;
	float *hdensity = NULL, *vdensity = NULL;
//	float *vweight, *hweight;
	float sumh1, sumh2, sumh3, sumv1, sumv2, sumv3;
	unsigned char *newline = NULL, *line;

    float center, start, *vweight, *hweight;
	int nmax;

	int t1, t2, t3, t4;

	init_gamma();

	MALLOC(newline, unsigned char, dst->rowsize);
	MALLOC(startx, int, dst->width);
	MALLOC(endx, int, dst->width);
	MALLOC(starty, int, dst->height);
	MALLOC(endy, int, dst->height);
	CALLOC(hweights, float *, dst->width);
	CALLOC(vweights, float *, dst->height);
	MALLOC(hdensity, float, dst->width);
	MALLOC(vdensity, float, dst->height);

	debug("scaling up: %dx%d => %dx%d", width, height,
			dst->width, dst->height);

    // pre-calculation for each column
	for (t1 = 0; t1 < dst->width; t1++)
	{
		center = ((float)t1 + 0.5f) * fx;

		startx[t1] = MAX(center - LANCZOS_WINDOW, 0);
		endx[t1] = MIN(ceil (center + LANCZOS_WINDOW), width - 1);
		nmax = endx[t1] - startx[t1];
        start = (float)startx[t1] + 0.5f - center;

		MALLOC(hweights[t1], float, nmax);
        hdensity[t1] = 0.0f;

		for (t2 = 0; t2 < nmax; t2++)
		{
            hweights[t1][t2] = lanczos((start + t2) / LANCZOS_BLUR);
			hdensity[t1] += hweights[t1][t2];
		}

        for (t2 = 0; t2 < nmax; t2++)
            hweights[t1][t2] /= hdensity[t1];
	}

	// pre-calculation for each row
	for (t1 = 0; t1 < dst->height; t1++)
	{
		center = ((float)t1 + 0.5f) * fy;

		starty[t1] = MAX(center - LANCZOS_WINDOW, 0);
		endy[t1] = MIN(ceil (center + LANCZOS_WINDOW), height - 1);
		nmax = endy[t1] - starty[t1];
        start = (float)starty[t1] + 0.5f - center;

		MALLOC(vweights[t1], float, nmax);
        vdensity[t1] = 0.0f;

		for (t2 = 0; t2 < nmax; t2++)
		{
			vweights[t1][t2] = lanczos((start + t2) / LANCZOS_BLUR);
			vdensity[t1] += vweights[t1][t2];
		}

        for (t2 = 0; t2 < nmax; t2++)
            vweights[t1][t2] /= vdensity[t1];
	}

	for (t1 = 0; t1 < dst->height; t1++)
	{
		int nmaxv = endy[t1] - starty[t1], nmaxh;
		line = newline;
//        unsigned char *line = newbuffer + t1 * newrowsize;

		for (t2 = 0; t2 < dst->width; t2++)
		{
			nmaxh = endx[t2] - startx[t2];
			unsigned char* ypos = buffer + starty[t1] * rowsize;

			sumv1 = sumv2 = sumv3 = 0.0f;
            vweight = vweights[t1];

			for (t3 = 0; t3 < nmaxv; t3++)
			{
				unsigned char *xpos = ypos + startx[t2] * bpp;

				sumh1 = sumh2 = sumh3 = 0.0f;
                hweight = hweights[t2];

				for (t4 = 0; t4 < nmaxh; t4++)
				{
					sumh1 += *hweight * togamma[xpos[0]];
					sumh2 += *hweight * togamma[xpos[1]];
					sumh3 += *hweight * togamma[xpos[2]];

					xpos += bpp;
                    hweight++;
				}

				sumv1 += *vweight * sumh1;
				sumv2 += *vweight * sumh2;
				sumv3 += *vweight * sumh3;

				ypos += rowsize;
                vweight++;
			}

			line[0] = fromgamma[(int)(CAP(sumv1) * GAMMASIZE)];
			line[1] = fromgamma[(int)(CAP(sumv2) * GAMMASIZE)];
			line[2] = fromgamma[(int)(CAP(sumv3) * GAMMASIZE)];

            line += dst->bpp;
		}

		if ((rc = dst->write_scanline(dst, newline))) {
			error("write_scanline: %s", strerror(rc));
			goto finally;
		}
    }

	rc = 0;

finally:
	FREE(startx);
	FREE(endx);
	FREE(starty);
	FREE(endy);
	FREE_ARRAY(hweights, dst->width);
	FREE_ARRAY(vweights, dst->height);
    FREE(hdensity);
    FREE(vdensity);
	FREE(newline);
	return rc;
}

int scale_down(struct imgsrc *src, struct imgdst *dst)
{
	int rc;
	float fx = (float) src->width / dst->width;
	float fy = (float) src->height / dst->height;
	int nrcols = (int)ceil(fx + 1.0f);
	float startx = 0.0f;
	float endx = fx;

	int nrrows = (int)ceil(fy + 1.0f);
	unsigned char **rows = NULL;
	float *dxA = NULL, *dxB = NULL, *dyA = NULL;
	int *ixA = NULL, *iyA = NULL, *nrxA = NULL;
	unsigned char *newline = NULL, *c1, *c2;
	float starty, endy = 0.0f, dy, diffY;
	int pos, nrx, nrx1, nry;
	float sum1, sum2, sum3, f, area;

	int t1, t2, t3, t4, t5, t6;
	float x, y;

	init_gamma();

	MALLOC(newline, unsigned char, dst->rowsize);
	MALLOC(rows, unsigned char *, nrrows);
	MALLOC(dxA, float, nrcols * dst->width);
	MALLOC(dxB, float, dst->width);
	MALLOC(dyA, float, nrrows);
	MALLOC(ixA, int, nrcols * dst->width);
	MALLOC(iyA, int, nrrows);
	MALLOC(nrxA, int, dst->width);

	nrx = 0;

	debug("scaling down: %dx%d => %dx%d", src->width, src->height,
			dst->width, dst->height);

	for (t1 = 0; t1 < dst->width; t1++)
	{
		endx = endx < src->width + 1 ? endx : endx - src->width + 1;

		for (x = startx; x < endx; x = floor(x + 1.0f))
		{
			ixA[nrx] = (int)x * src->bpp;

			if (endx - x > 1.0f)
				dxA[nrx] = floor(x + 1.0f) - x;
			else
				dxA[nrx] = endx - x;

			nrx++;
		}

		if (t1 > 0)
			nrxA[t1] = nrx - nrx1;
		else
			nrxA[t1] = nrx;

		nrx1 = nrx;

		dxB[t1] = endx - startx;

		startx = endx;
		endx += fx;
	}

//	warn("%s(%d)", __func__, __LINE__);

	for (t1 = 0; t1 < dst->height; t1++)
	{
//		warn("scanline: %d", t1);

		pos = t1 * fy;
		t3 = pos + nrrows < src->height + 1 ? 0 : pos - src->height + 1;

		for (t2 = 0; t2 < nrrows + t3; t2++)
		{
			rows[t2] = src->read_scanline(src, pos + t2);
		}

//		warn("%s(%d)", __func__, __LINE__);

		starty = t1 * fy - pos;
		endy = (t1 + 1) * fy - pos + t3;
		diffY = endy - starty;
		nry = 0;

		for (y = starty; y < endy; y = floor(y + 1.0f))
		{
//			warn("%s(%d)", __func__, __LINE__);
			iyA[nry] = (int)y;
			if (endy - y > 1.0f)
				dyA[nry] = floor(y + 1.0f) - y;
			else
				dyA[nry] = endy - y;

			nry++;
		}

		nrx = 0;
		t6 = 0;

		for (t2 = 0; t2 < dst->width; t2++)
		{
//			warn("%s(%d)", __func__, __LINE__);
			t3 = nrxA[t2];
			area = (1.0f / (dxB[t2] * diffY)) * GAMMASIZE;
			sum1 = sum2 = sum3 = 0.0f;

			for (t4 = 0; t4 < nry; t4++)
			{
//				warn("%s(%d)", __func__, __LINE__);
				c1 = rows[iyA[t4]];
				dy = dyA[t4];

				nrx1 = nrx;

				for (t5 = 0; t5 < t3; t5++)
				{
//					warn("%s(%d)", __func__, __LINE__);
					f = dxA[nrx1] * dy;
					c2 = c1 + ixA[nrx1];

					sum1 += togamma[c2[0]] * f;
					sum2 += togamma[c2[1]] * f;
					sum3 += togamma[c2[2]] * f;

					nrx1++;
				}
			}
//			warn("%s(%d)", __func__, __LINE__);

			newline[t6++] = fromgamma[(int)(sum1 * area)];
			newline[t6++] = fromgamma[(int)(sum2 * area)];
			newline[t6++] = fromgamma[(int)(sum3 * area)];

			nrx += t3;
		}

//		warn("%s(%d)", __func__, __LINE__);

		if ((rc = dst->write_scanline(dst, newline))) {
			error("write_scanline: %s", strerror(rc));
			goto finally;
		}
	}

	rc = 0;

finally:
	FREE(iyA);
	FREE(nrxA);
	FREE(ixA);
	FREE(dyA);
	FREE(dxB);
	FREE(dxA);
	FREE(rows);
	FREE(newline);
	return rc;
}

#if 0
int resize(FILE *f1, int w1, int h1, int bpp1,
		FILE *f2, int w2, int h2, int bpp2)
{
	int rc;
	struct imgsrc *src = NULL;
	struct imgdst *dst = NULL;
	unsigned char *buffer = NULL, *newbuffer = NULL;
	size_t len;

	float fx = (float) w1 / w2;
	float fy = (float) h1 / h2;

	float rowsize = rowstride(bpp1, w1);

	init_gamma();

	if (fx > 1.0 || fy > 1.0) {
		warn("scaling down: %dx%d => %dx%d", w1, h1, w2, h2);

		float fx = (float) w1 / w2;
		float fy = (float) h1 / h2;
		int nrrows = (int)ceil(fy + 1.0f);
		int pos = h2 / 2 * fy;
		int t3 = pos + nrrows < h1 + 1 ? 0 : pos - h1 + 1;

//		warn("%s(%d)", __func__, __LINE__);

		if (!(src = fiosrc_new(f1, w1, h1, bpp1, nrrows + t3))) {
			rc = errno ? errno : -1;
			error("fiosrc_new: %s", strerror(rc));
			goto finally;
		}

//		warn("%s(%d)", __func__, __LINE__);

		if (!(dst = fiodst_new(f2, w2, h2, bpp2))) {
			rc = errno ? errno : -1;
			error("fiodst_new: %s", strerror(rc));
			goto finally;
		}

//		warn("%s(%d)", __func__, __LINE__);

		if ((rc = scaledown(src, dst, fx, fy))) {
			error("scaledown: %s", strerror(rc));
			goto finally;
		}
	} else {
		warn("scaling up: %dx%d => %dx%d", w1, h1, w2, h2);

		len = w1 * h1 * bpp1;

		if (!(buffer = malloc(len))) {
			rc = errno ? errno : -1;
			error("malloc: %s", strerror(rc));
			goto finally;
		}

		if (fread(buffer, 1, len, f1) != len) {
			rc = errno ? errno : -1;
			error("fread: %s", strerror(rc));
			goto finally;
		}

#if 1
		if (!(dst = fiodst_new(f2, w2, h2, bpp2))) {
			rc = errno ? errno : -1;
			error("fiodst_new: %s", strerror(rc));
			goto finally;
		}
#else
		len = w2 * h2 * bpp2;

		if (!(newbuffer = malloc(len))) {
			rc = errno ? errno : -1;
			error("malloc: %s", strerror(rc));
			goto finally;
		}

		if (!(dst = memdst_new(newbuffer, w2, h2, bpp2))) {
			rc = errno ? errno : -1;
			error("memdst_new: %s", strerror(rc));
			goto finally;
		}
#endif

		if ((rc = scaleup(buffer, w1, h1, bpp1, rowsize,
				dst, fx, fy))) {
			error("scaleup: %s", strerror(rc));
			goto finally;
		}

		if (newbuffer && (fwrite(newbuffer, 1, len, f2)) != len) {
			rc = errno ? errno : -1;
			error("fwrite: %s", strerror(rc));
			goto finally;
		}
	}

	rc = 0;

finally:
	if (dst) {
		if (newbuffer) {
			memdst_free(dst);
		} else {
			fiodst_free(dst);
		}
	}
	if (src) {
		if (buffer) {
			memsrc_free(src);
		} else {
			fiosrc_free(src);
		}
	}
	FREE(buffer);
	FREE(newbuffer);
	return rc;
}
#endif

