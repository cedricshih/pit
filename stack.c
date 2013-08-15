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

static int load_file(float *dst, const char *src,
		size_t w, size_t h)
{
	int rc;
	FILE *fdst = NULL, *fsrc = NULL;
	size_t i, y, stride, len;
	unsigned char *buffer = NULL;

	stride = w * 3;

	if (!(buffer = malloc(stride))) {
		rc = errno ? errno : -1;
		error("malloc: %s", strerror(rc));
		goto finally;
	}

	if (!(fsrc = fopen(src, "rb"))) {
		rc = errno ? errno : -1;
		error("fopen: %s", strerror(rc));
		goto finally;
	}

	for (y = 0; y < h; y++) {
		if ((len = fread(buffer, 1, stride, fsrc)) != stride) {
			rc = errno ? errno : -1;
			error("fread: %s", strerror(rc));
			goto finally;
		}

		for (i = 0; i < stride; i++) {
			*dst++ += buffer[i] / 255.0;
		}
	}

	rc = 0;

finally:
	if (buffer) {
		free(buffer);
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

static int stack_file(float *bg, const char *filename, size_t w, size_t h,
		float ev, float lo, float hi)
{
	int rc;
	FILE *file = NULL;
	size_t i, y, stride, len;
	unsigned char *bytes = NULL;
	float factor, *fg, *mat, *dst;

	stride = w * 3;

	if (!(bytes = malloc(stride))) {
		rc = errno ? errno : -1;
		error("malloc: %s", strerror(rc));
		goto finally;
	}

	len = stride * sizeof(*fg);

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

	if (!(file = fopen(filename, "rb"))) {
		rc = errno ? errno : -1;
		error("fopen: %s", strerror(rc));
		goto finally;
	}

	factor = ev_factor(ev);

	for (y = 0; y < h; y++) {
		if ((len = fread(bytes, 1, stride, file)) != stride) {
			rc = errno ? errno : -1;
			error("fread: %s", strerror(rc));
			goto finally;
		}

//		for (i = 0; i < stride; i++) {
//			*bg++ += bytes[i] / 255.0;
//		}

		dst = fg;

		for (i = 0; i < stride; i++) {
			*dst++ = bytes[i] / 255.0;
		}

		stack_line(bg, fg, mat, stride, factor, lo, hi);

//		dst = bg;
//
//		for (i = 0; i < stride; i++) {
//			*dst++ *= pow(2.0, center);
//		}

		bg += stride;
	}

	rc = 0;

finally:
	if (mat) {
		free(mat);
	}
	if (fg) {
		free(fg);
	}
	if (bytes) {
		free(bytes);
	}
	if (file) {
		fclose(file);
	}
	return rc;
}

static int write_file(float *src, const char *filename, size_t w, size_t h)
{
	int rc;
	FILE *file = NULL;

	if (!(file = fopen(filename, "w+"))) {
		rc = errno ? errno : -1;
		error("fopen: %s", strerror(rc));
		goto finally;
	}

	if (RGBE_WriteHeader(file, w, h, NULL)) {
		rc = errno ? errno : -1;
		error("RGBE_WriteHeader: %s", strerror(rc));
		goto finally;
	}

	if (RGBE_WritePixels(file, src, w * h)) {
		rc = errno ? errno : -1;
		error("RGBE_WritePixels: %s", strerror(rc));
		goto finally;
	}

	rc = 0;

finally:
	if (file) {
		fclose(file);
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
	size_t total, count, w, h, n, evnum;
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
			snprintf(fmt, sizeof(fmt), "%d", total);
			snprintf(fmt, sizeof(fmt), "%%0%dd", strlen(fmt));

			fprintf(stdout, fmt, count + 1);
			fprintf(stdout, "/%d: ", total / evnum);

			RB_FOREACH(layer, stack, &stack) {
				fprintf(stdout, "%s => ", layer->path);

				if ((rc = jpg2rgb(layer->path, TEMPFILE_RGB, 0, 255, 1, 0, &w, &h))) {
					error("jpg2rgb: %s", strerror(rc));
					goto finally;
				} else {
					n = w * h * 3;
				}

				if (layer == RB_MIN(stack, &stack)) {
					if (!buffer) {
						if (!(buffer = calloc(n, sizeof(*buffer)))) {
							rc = errno ? errno : -1;
							error("calloc: %s", strerror(rc));
							goto finally;
						}
					}

					if ((rc = load_file(buffer, TEMPFILE_RGB, w, h))) {
						error("load_file: %s", strerror(rc));
						goto finally;
					}
				} else {
					if ((rc = stack_file(buffer, TEMPFILE_RGB, w, h,
							layer->stop - evmin, 0.7, 0.8))) {
						error("stack_file: %s", strerror(rc));
						goto finally;
					}
				}
			}

			nmultiply(buffer, buffer, n, pow(2.0, evstop));

			count++;
			snprintf(fmt, sizeof(fmt), output, count);

			fprintf(stdout, "%s => ", fmt);

			if ((rc = write_file(buffer, fmt, w, h))) {
				error("write_file: %s", strerror(rc));
				goto finally;
			}

			fprintf(stdout, "OK\n");

			ev = TAILQ_FIRST(&evlist);
			stack_clear(&stack);
		}
//
//		fprintf(stdout, "OK\n");
//
//		count++;
//
//		if (count >= total) {
//			break;
//		}
//
//		snprintf(fmt, sizeof(fmt), "%d", total);
//		snprintf(fmt, sizeof(fmt), "%%0%dd", strlen(fmt));
//
//		fprintf(stdout, fmt, count + 1);
//		fprintf(stdout, "/%d: %s => ", total, item->path);
//
//		if ((rc = jpg2rgb(item->path, TEMPFILE, 0, 255, 1, 0, &w, &h))) {
//			error("jpg2rgb: %s", strerror(rc));
//			goto finally;
//		} else {
//			n = w * h * 3;
//		}
//
//		if (ev == TAILQ_FIRST(&evlist)) {
//			if (!buffer) {
//				if (!(buffer = calloc(n, sizeof(*buffer)))) {
//					rc = errno ? errno : -1;
//					error("calloc: %s", strerror(rc));
//					goto finally;
//				}
//			}
//
//			if ((rc = load_file(buffer, TEMPFILE, w, h))) {
//				error("load_file: %s", strerror(rc));
//				goto finally;
//			}
//		} else {
//			fprintf(stdout, "EV=%f/%f => ", ev->stop, ev->stop - evmin);
//
//			if ((rc = stack_file(buffer, TEMPFILE, w, h,
//					ev->stop - evmin, 0.7, 0.8))) {
//				error("stack_file: %s", strerror(rc));
//				goto finally;
//			}
//		}
//
//		ev = TAILQ_NEXT(ev, next);
//
//		if (!ev) {
//			nmultiply(buffer, buffer, n, pow(2.0, evstop));
//
//			outnum++;
//			snprintf(fmt, sizeof(fmt), output, outnum - evmin);
//
//			fprintf(stdout, "%s => ", fmt);
//
//			if ((rc = write_file(buffer, fmt, w, h))) {
//				error("write_file: %s", strerror(rc));
//				goto finally;
//			}
//
//			ev = TAILQ_FIRST(&evlist);
//		}
//
//		fprintf(stdout, "OK\n");
//
//		count++;
//
//		if (count >= total) {
//			break;
//		}
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
