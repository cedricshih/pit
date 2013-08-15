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

#include <sys/queue.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <getopt.h>
#include <math.h>
#include <dirent.h>
#include <unistd.h>
#include <libgen.h>
#include <sys/stat.h>

#include "log.h"
#include "common.h"
#include "filelist.h"
#include "jpg2rgb.h"
#include "rgb2jpg.h"
#include "rgbe.h"

#define DEFAULT_OUTPUT "stack_%05d.hdr"
#define TEMPFILE_RGB "tempfile.rgb"
#define TEMPFILE_RGBF "tempfile.xyz"

#define murmur(fmt...) fprintf(stderr, fmt)

struct ev {
	float stop;
	TAILQ_ENTRY(ev) next;
};

TAILQ_HEAD(evlist, ev);

struct layer {
	float stop;
	const char *path;
	RB_ENTRY(layer) entry;
};

static int layer_cmp(struct layer *a, struct layer *b);

RB_HEAD(stack, layer);
RB_PROTOTYPE(stack, layer, entry, layer_cmp);

static void stack_clear(struct stack *stack);
static int stack_flush(struct stack *stack, float evcenter, float evmin,
		const char *output);

void stack_help(FILE *file, char *basename, char *cmd)
{
	fprintf(file, "Usage: %s %s [options] [file...]\n\n"
			"Options:\n"
			"    -o <output>         Output filename template (default: %s)\n"
			"    -e <stop>           Bracket EV (specify for multiple times)\n"
			"    -t <begin>:<end>    Treat file name as template, e.g. '%%08d.JPG'\n"
			"\n", basename, cmd, DEFAULT_OUTPUT);
}

static int jpeg_filter(const char *filename, const char *extname, void *cbarg)
{
	const char *output = cbarg;

	if (strcmp(output, filename) && extname &&
			(!strcasecmp(extname, "jpg") ||
			!strcasecmp(extname, "jpeg"))) {
		return 1;
	} else {
		return 0;
	}
}

static float ev_factor(float ev)
{
    if (ev <= 0) {
        return pow(2.0, abs(ev));
    } else {
        return pow(0.5, ev);
    }
}

static inline float clamp(float p, float lo, float hi)
{
	if (p > hi) {
		return hi;
	} else if (p < lo) {
		return lo;
	} else {
		return p;
	}
}

static inline void nclamp(float *dst, float *src, size_t len, float lo, float hi)
{
	int i;

	for (i = 0; i < len; i++) {
		*dst++ = clamp(*src++, lo, hi);
	}
}

static inline void nset(float *dst, size_t len, float v)
{
	int i;

	for (i = 0; i < len; i++) {
		*dst++ = v;
	}
}

static inline float clip(float p, float lo, float hi)
{
	if (p >= hi) {
		return 0.0;
	} else if (p < lo) {
		return 1.0;
	} else {
		return (hi + (-1 * p)) / (hi - lo);
	}
}

static inline void nclip(float *dst, float *src, size_t len, float lo, float hi)
{
	int i;

	for (i = 0; i < len; i++) {
		*dst++ = clip(*src++, lo, hi);
//		*dst++ = *src++;
	}
}

static inline void nmultiply(float *dst, float *src, size_t len, float x)
{
	int i;

	for (i = 0; i < len; i++) {
		*dst++ = *src++ * x;
	}
}

static inline void nnmultiply(float *dst, float *x, float *y, size_t len)
{
	int i;

	for (i = 0; i < len; i++) {
		*dst++ = *x++ * *y++;
	}
}

static inline void nsubtract(float *dst, float *src, size_t len, float x)
{
	int i;

	for (i = 0; i < len; i++) {
		*dst++ = x - *src++;
	}
}

static inline void nnadd(float *dst, float *x, float *y, size_t len)
{
	int i;

	for (i = 0; i < len; i++) {
		*dst++ = *x++ + *y++;
	}
}

static inline int freadn(void *dst, FILE *src, size_t len)
{
	int rc;
	size_t n, rem = len;
	unsigned char *ptr = dst;

	clearerr(src);

	while (rem > 0) {
		if ((n = fread(ptr, 1, rem, src)) != rem) {
			if ((rc = ferror(src))) {
				goto finally;
			}

			if (!n) {
				rc = errno ? errno : -1;
				goto finally;
			}
		}

		rem -= n;
		ptr += n;
	}

	rc = 0;

finally:
	return rc;
}

static int load_file(const char *dst, const char *src, size_t w, size_t h)
{
	int rc;
	FILE *fdst = NULL, *fsrc = NULL;
	size_t i, y, stride, len;
	float *bdst = NULL;
	unsigned char *bsrc = NULL;

	stride = w * 3;

	if (!(bdst = malloc(stride * sizeof(*bdst)))) {
		rc = errno ? errno : -1;
		error("malloc: %s", strerror(rc));
		goto finally;
	}

	if (!(bsrc = malloc(stride))) {
		rc = errno ? errno : -1;
		error("malloc: %s", strerror(rc));
		goto finally;
	}

	if (!(fsrc = fopen(src, "rb"))) {
		rc = errno ? errno : -1;
		error("fopen: %s", strerror(rc));
		goto finally;
	}

	if (!(fdst = fopen(dst, "wb+"))) {
		rc = errno ? errno : -1;
		error("fopen: %s", strerror(rc));
		goto finally;
	}

	for (y = 0; y < h; y++) {
		if ((rc = freadn(bsrc, fsrc, stride))) {
			error("fread: %s", strerror(rc));
			goto finally;
		}

		for (i = 0; i < stride; i++) {
			bdst[i] = bsrc[i] / 255.0;
		}

		if ((len = fwrite(bdst, sizeof(*bdst), stride, fdst)) != stride) {
			rc = errno ? errno : -1;
			error("fread: %s", strerror(rc));
			goto finally;
		}
	}

	rc = 0;

finally:
	if (bsrc) {
		free(bsrc);
	}
	if (bdst) {
		free(bdst);
	}
	if (fsrc) {
		fclose(fsrc);
	}
	if (fdst) {
		fclose(fdst);
	}
	return rc;
}

static void stack_line(float *bg, float *fg, float *mat, size_t len,
		float factor, float lo, float hi)
{
	nclip(mat, fg, len, lo, hi);
	nmultiply(fg, fg, len, factor);
	nnmultiply(fg, fg, mat, len);
	nsubtract(mat, mat, len, 1.0);
	nnmultiply(bg, bg, mat, len);
	nnadd(bg, bg, fg, len);
}

#if 0
static int read_line(float *dst, const char *src, size_t y, size_t stride)
{
	int rc;
	FILE *fsrc = NULL;
	size_t len;

	if (!(fsrc = fopen(src, "rb"))) {
		rc = errno ? errno : -1;
		error("fopen: %s", strerror(rc));
		goto finally;
	}

	if (y != 0) {
		len = y * stride * sizeof(*dst);

		fprintf(stdout, "reading line: %d @ %d\n", y, len);

		if (fseek(fsrc, len, SEEK_SET) < 0) {
			rc = errno ? errno : -1;
			error("fseek: %s (%s)", strerror(rc), src);
			goto finally;
		}
	}

	if ((rc = freadn(dst, fsrc, stride * sizeof(*dst)))) {
		error("fread: %s", strerror(rc));
		goto finally;
	}

	rc = 0;

finally:
	if (fsrc) {
		fclose(fsrc);
	}
	return rc;
}

static int write_line(const char *dst, float *src, size_t y, size_t stride)
{
	int rc;
	FILE *fdst = NULL;
	size_t len;

	if (!(fdst = fopen(dst, "wb"))) {
		rc = errno ? errno : -1;
		error("fopen: %s", strerror(rc));
		goto finally;
	}

//	fprintf(stdout, "writing line: %d\n", y);

	if (y != 0) {
		if (fseek(fdst, y * stride * sizeof(*dst), SEEK_SET) < 0) {
			rc = errno ? errno : -1;
			error("fseek: %s", strerror(rc));
			goto finally;
		}
	}

	if ((len = fwrite(src, sizeof(*src), stride, fdst)) != stride) {
		rc = errno ? errno : -1;
		error("fwrite: %s", strerror(rc));
		goto finally;
	}

	rc = 0;

finally:
	if (fdst) {
		fflush(fdst);
		fclose(fdst);
	}
	return rc;
}
#endif

static int stack_file(const char *dst, const char *src, size_t w, size_t h,
		float ev, float lo, float hi)
{
	int rc;
	FILE *fdst = NULL, *fsrc = NULL;
	size_t i, y, stride, len, off;
	unsigned char *bytes = NULL;
	float factor, *bg = NULL, *fg = NULL, *mat = NULL;

	stride = w * 3;

	if (!(bytes = malloc(stride))) {
		rc = errno ? errno : -1;
		error("malloc: %s", strerror(rc));
		goto finally;
	}

	len = stride * sizeof(*bg);

	if (!(bg = malloc(len))) {
		rc = errno ? errno : -1;
		error("malloc: %s", strerror(rc));
		goto finally;
	}

	if (!(fg = malloc(len))) {
		rc = errno ? errno : -1;
		error("malloc: %s", strerror(rc));
		goto finally;
	}

	if (!(mat = malloc(len))) {
		rc = errno ? errno : -1;
		error("malloc: %s", strerror(rc));
		goto finally;
	}

	if (!(fsrc = fopen(src, "rb"))) {
		rc = errno ? errno : -1;
		error("fopen: %s", strerror(rc));
		goto finally;
	}

	if (!(fdst = fopen(dst, "r+"))) {
		rc = errno ? errno : -1;
		error("fopen: %s", strerror(rc));
		goto finally;
	}

	factor = ev_factor(ev);

	for (y = 0; y < h; y++) {
		if ((rc = freadn(bytes, fsrc, stride))) {
			error("fread: %s", strerror(rc));
			goto finally;
		}

		for (i = 0; i < stride; i++) {
			fg[i] = bytes[i] / 255.0;
		}

		off = y * stride * sizeof(*bg);
		fseek(fdst, off, 0);

		if ((rc = freadn(bg, fdst, stride * sizeof(*bg)))) {
			error("fread: %s", strerror(rc));
			goto finally;
		}

//		if ((rc = read_line(bg, dst, y, stride))) {
//			error("read_line: %s", strerror(rc));
//			goto finally;
//		}

		stack_line(bg, fg, mat, stride, factor, lo, hi);

		fseek(fdst, off, 0);

		if ((len = fwrite(bg, sizeof(*bg), stride, fdst)) != stride) {
			rc = errno ? errno : -1;
			error("fwrite: %s", strerror(rc));
			goto finally;
		}

		fflush(fdst);

//		if ((rc = write_line(dst, bg, y, stride))) {
//			error("write_line: %s", strerror(rc));
//			goto finally;
//		}
	}

	rc = 0;

finally:
	if (mat) {
		free(mat);
	}
	if (bg) {
		free(bg);
	}
	if (fg) {
		free(fg);
	}
	if (bytes) {
		free(bytes);
	}
	if (fdst) {
		fclose(fdst);
	}
	if (fsrc) {
		fclose(fsrc);
	}
	return rc;
}

static int write_file(const char *dst, const char *src, size_t w, size_t h, float factor)
{
	int rc, y;
	FILE *fdst = NULL, *fsrc = NULL;
	float *buffer = NULL;
	size_t stride = w * 3;

	if (!(fdst = fopen(dst, "wb+"))) {
		rc = errno ? errno : -1;
		error("fopen: %s", strerror(rc));
		goto finally;
	}

	if (!(fsrc = fopen(src, "rb"))) {
		rc = errno ? errno : -1;
		error("fopen: %s", strerror(rc));
		goto finally;
	}

	if (!(buffer = malloc(stride * sizeof(*buffer)))) {
		rc = errno ? errno : -1;
		error("malloc: %s", strerror(rc));
		goto finally;
	}

	if (RGBE_WriteHeader(fdst, w, h, NULL)) {
		rc = errno ? errno : -1;
		error("RGBE_WriteHeader: %s", strerror(rc));
		goto finally;
	}

	for (y = 0; y < h; y++) {
		if ((rc = freadn(buffer, fsrc, stride * sizeof(*buffer)))) {
			error("fread: %s (y=%d)", strerror(rc), y);
			goto finally;
		}

		nmultiply(buffer, buffer, stride, factor);

		if (RGBE_WritePixels(fdst, buffer, w)) {
			rc = errno ? errno : -1;
			error("RGBE_WritePixels: %s", strerror(rc));
			goto finally;
		}
	}

	rc = 0;

finally:
	if (fdst) {
		fclose(fdst);
	}
	if (fsrc) {
		fclose(fsrc);
	}
	return rc;
}

int stack(char *basename, int argc, char **argv)
{
	int rc, c, i, j;
	enum pit_log_level log_level = PIT_WARN;
	struct pit_range range;
	struct fileque list;
	struct file *item;
	struct evlist evlist;
	struct ev *ev;
	struct stack stack;
	struct layer *layer;
	size_t total, count, evnum;
	char *ptr, *output = DEFAULT_OUTPUT;
	char fmt[PATH_MAX];
	float *buffer, evmin, evstop;

	buffer = NULL;
	TAILQ_INIT(&list);
	TAILQ_INIT(&evlist);
	RB_INIT(&stack);
	evnum = 0;

	memset(&range, '\0', sizeof(range));
	range.lo.value = -1;
	range.hi.value = -1;

	while ((c = getopt(argc, argv, "vo:e:t:")) != -1) {
		switch (c) {
		case 'v':
			log_level--;
			break;
		case 'o':
			output = optarg;
			break;
		case 'e':
			ptr = optarg;

			while (ptr) {
				evstop = strtof(ptr, &ptr);

				if (!(ev = calloc(1, sizeof(*ev)))) {
					rc = errno ? errno : -1;
					error("calloc: %s", strerror(rc));
					goto finally;
				}

				ev->stop = evstop;

				TAILQ_INSERT_TAIL(&evlist, ev, next);
				evnum++;

				if (*ptr == ',') {
					ptr++;
				} else {
					ptr = NULL;
				}
			}
			break;
		case 't':
			if ((rc = pit_range_parse(&range, optarg))) {
				murmur("Invalid range of template: %s\n", optarg);
				goto finally;
			}
			break;
		default:
			/* unrecognised option ... add your error condition */
			break;
		}
	}

	argc -= optind;
	argv += optind;

	pit_set_log_level(log_level);

	if (argc == 0) {
		if ((rc = fileque_list(&list, ".", &total, jpeg_filter,
				output))) {
			error("fileque_list: %s", strerror(rc));
			goto finally;
		}
	} else {
		total = 0;

		for (i = 0; i < argc; i++) {
			if (range.lo.value != -1 && range.hi.value != -1) {
				for (j = range.lo.value; j <= range.hi.value; j++) {
					snprintf(fmt, sizeof(fmt), argv[i], j);

					if ((rc = fileque_add(&list, fmt))) {
						if (rc == ENOENT) {
							fprintf(stderr, "no such file: %s", fmt);
							continue;
						} else if (rc == EEXIST) {
							fprintf(stderr, "exists: %s", fmt);
							continue;
						}

						error("fileque_add: %s", strerror(rc));
						goto finally;
					}

					total++;
				}
			} else {
				if ((rc = fileque_add(&list, argv[i]))) {
					if (rc == EEXIST) {
						continue;
					}

					error("fileque_add: %s", strerror(rc));
					goto finally;
				}

				total++;
			}
		}
	}

	if (TAILQ_EMPTY(&list)) {
		murmur("No input file.\n");
		rc = EINVAL;
		goto finally;
	}

	if (TAILQ_EMPTY(&evlist)) {
		murmur("No EV stop specified.\n");
		rc = EINVAL;
		goto finally;
	}

	if (evnum == 1) {
		murmur("Only one EV stop specified.\n");
		rc = EINVAL;
		goto finally;
	}

	if (total % evnum != 0) {
		murmur("Number of files could not be divided by number of EV stops.\n");
		rc = EINVAL;
		goto finally;
	}

	evmin = 1e20F;
	evstop = 1e20F;

	TAILQ_FOREACH(ev, &evlist, next) {
		if (abs(ev->stop) < evstop) {
			evstop = abs(ev->stop);
		}

		if (ev->stop < evmin) {
			evmin = ev->stop;
		}
	}

	fprintf(stdout, "Center EV: %f\n", evstop);

	count = 0;
	ev = TAILQ_FIRST(&evlist);

	TAILQ_FOREACH(item, &list, next) {
		if (!(layer = calloc(1, sizeof(*layer)))) {
			rc = errno ? errno : -1;
			error("calloc: %s", strerror(rc));
			goto finally;
		}

		layer->stop = ev->stop;
		layer->path = item->path;
		RB_INSERT(stack, &stack, layer);

		if (!(ev = TAILQ_NEXT(ev, next))) {
			snprintf(fmt, sizeof(fmt), "%d", total / evnum);
			snprintf(fmt, sizeof(fmt), "%%0%dd", strlen(fmt));

			fprintf(stdout, fmt, count + 1);
			fprintf(stdout, "/%d: ", total / evnum);

			count++;
			snprintf(fmt, sizeof(fmt), output, count);

			stack_flush(&stack, evstop, evmin, fmt);

			fprintf(stdout, "OK\n");

			ev = TAILQ_FIRST(&evlist);
		}
	}

	rc = 0;

finally:
	unlink(TEMPFILE_RGB);
	unlink(TEMPFILE_RGBF);
	while ((ev = TAILQ_FIRST(&evlist))) {
		TAILQ_REMOVE(&evlist, ev, next);
		free(ev);
	}
	if (buffer) {
		free(buffer);
	}
	fileque_clear(&list);
	return rc;
}

int stack_flush(struct stack *stack, float evcenter, float evmin,
		const char *output)
{
	int rc;
	struct layer *layer;
	size_t w, h;

	RB_FOREACH(layer, stack, stack) {
		fprintf(stdout, "%s => ", layer->path);

		if ((rc = jpg2rgb(layer->path, TEMPFILE_RGB, 0, 255, 1, 0, &w, &h))) {
			error("jpg2rgb: %s", strerror(rc));
			goto finally;
		}

		if (layer == RB_MIN(stack, stack)) {
			if ((rc = load_file(TEMPFILE_RGBF, TEMPFILE_RGB, w, h))) {
				error("load_file: %s", strerror(rc));
				goto finally;
			}
		} else {
			if ((rc = stack_file(TEMPFILE_RGBF, TEMPFILE_RGB, w, h,
					layer->stop - evmin, 0.7, 0.8))) {
				error("stack_file: %s", strerror(rc));
				goto finally;
			}
		}
	}

	fprintf(stdout, "%s => ", output);

	if ((rc = write_file(output, TEMPFILE_RGBF, w, h,
			pow(2.0, evcenter - evmin)))) {
		error("write_file: %s", strerror(rc));
		goto finally;
	}

	stack_clear(stack);
	rc = 0;

finally:
	return rc;
}

int layer_cmp(struct layer *a, struct layer *b)
{
	float result = a->stop - b->stop;

	if (result < 0) {
		return -1;
	} else if (result > 0) {
		return 1;
	} else {
		return 0;
	}
}

void stack_clear(struct stack *stack)
{
	struct layer *layer;

	while ((layer = RB_MIN(stack, stack))) {
		RB_REMOVE(stack, stack, layer);
		free(layer);
	}
}

RB_GENERATE(stack, layer, entry, layer_cmp);
