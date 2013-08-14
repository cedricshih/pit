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
#include "rgbe.h"

#define DEFAULT_OUTPUT "stack.hdr"
#define DEFAULT_EV 3

#define murmur(fmt...) fprintf(stderr, fmt)

void stack_help(FILE *file, char *basename, char *cmd)
{
	fprintf(file, "Usage: %s %s [options] [file...]\n\n"
			"Options:\n"
			"    -o <output>         Output file (default: %s)\n"
			"    -e <stop>           Bracket EV (default: %d)\n"
			"    -t <begin>:<end>    Treat file name as template, e.g. '%%08d.JPG'.\n"
			"\n", basename, cmd, DEFAULT_EV, DEFAULT_OUTPUT);
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

static int load_file(float *dst, const char *filename,
		size_t w, size_t h)
{
	int rc;
	FILE *file = NULL;
	size_t i, y, stride, len;
	unsigned char *buffer = NULL;

	stride = w * 3;

	if (!(buffer = malloc(stride))) {
		rc = errno ? errno : -1;
		error("malloc: %s", strerror(rc));
		goto finally;
	}

	if (!(file = fopen(filename, "rb"))) {
		rc = errno ? errno : -1;
		error("fopen: %s", strerror(rc));
		goto finally;
	}

	for (y = 0; y < h; y++) {
		if ((len = fread(buffer, 1, stride, file)) != stride) {
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
	if (file) {
		fclose(file);
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

	fprintf(stdout, "EV=%f/%f => ", ev, factor);

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
	size_t total, count, w, h, n;
	char *output = DEFAULT_OUTPUT;
	char fmt[PATH_MAX];
	float *buffer;
	int ev, step = DEFAULT_EV;

	buffer = NULL;
	TAILQ_INIT(&list);

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
			step = strtol(optarg, NULL, 10);
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

	count = 0;
	ev = 0;

	TAILQ_FOREACH(item, &list, next) {
		snprintf(fmt, sizeof(fmt), "%d", total);
		snprintf(fmt, sizeof(fmt), "%%0%dd", strlen(fmt));

		fprintf(stdout, fmt, count + 1);
		fprintf(stdout, "/%d: %s => ", total, item->path);

		if ((rc = jpg2rgb(item->path, output, 0, 255, 1, 0, &w, &h))) {
			error("jpg2rgb: %s", strerror(rc));
			goto finally;
		} else {
			n = w * h * 3;
		}

		if (!buffer) {
			if (!(buffer = calloc(n, sizeof(*buffer)))) {
				rc = errno ? errno : -1;
				error("calloc: %s", strerror(rc));
				goto finally;
			}

//			nset(buffer, n, 0);

			if ((rc = load_file(buffer, output, w, h))) {
				error("stack_file: %s", strerror(rc));
				goto finally;
			}
		} else {

		if ((rc = stack_file(buffer, output, w, h, ev, 0.7, 0.8))) {
			error("stack_file: %s", strerror(rc));
			goto finally;
		}
		}

		fprintf(stdout, "OK\n");

		count++;

		if (count >= total) {
			break;
		}

//		switch (ev) {
//		case 0:
//			ev = -3;
//			break;
//		case -3:
//			ev = 3;
//			break;
//		case 3:
//			ev = 0;
//			break;
//		}

		ev += step;
	}

	nmultiply(buffer, buffer, n, pow(2.0, 3));
//	nclamp(buffer, buffer, n, 0, 1.0);

	if ((rc = write_file(buffer, output, w, h))) {
		error("write_file: %s", strerror(rc));
		goto finally;
	}

	rc = 0;

finally:
	if (buffer) {
		free(buffer);
	}
	fileque_clear(&list);
	return rc;
}
