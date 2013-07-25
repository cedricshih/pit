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
#include "histogram.h"

#define murmur(fmt...) fprintf(stderr, fmt)

#define DEFAULT_QUALITY 98

#define PIXEL_MIN 0
#define PIXEL_MAX 255

void stretch_help(FILE *file, char *basename, char *cmd)
{
	fprintf(file, "Usage: %s %s [options] [file...]\n\n"
			"Options:\n"
			"    -o <output>         Output JPEG file\n"
			"    -q <quality>        Output JPEG quality from 0 to 100 (default: %d)\n"
			"    -c <black>[:white]  Stretch contrast; black and white points could be pixel value or percentage calculated from first frame.\n"
			"    -t <begin>:<end>    Treat file name as template, e.g. '%%08d.JPG'.\n"
			"\n", basename, cmd, DEFAULT_QUALITY);
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

#define MAX(a,b) a > b ? a : b

static int load_file(unsigned char *dst, const char *filename, size_t w, size_t h)
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
			*dst++ = MAX(buffer[i], *dst);
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

static int stretch_file(const char *filename, struct pit_range *contrast,
		const char *output, int quality)
{
	int rc, black, white;
	struct pit_dim size;
	size_t num;
	unsigned char *out = NULL;
	struct histogram *histogram = NULL;
	FILE *file = NULL;
	char rgb[PATH_MAX];

	snprintf(rgb, sizeof(rgb), "decompressed.rgb");
	memset(&size, '\0', sizeof(size));
	output = output ? output : filename;

	if ((rc = jpg_read_header(filename, &size.width, &size.height))) {
		error("jpg_read_header: %s", strerror(rc));
		goto finally;
	}

	num = size.width * size.height * 3;

	if (!(out = calloc(sizeof(*out), num))) {
		error("malloc: %s", strerror(rc));
		goto finally;
	}

	if ((rc = jpg2rgb(filename, rgb, 0, 255, 1, 0))) {
		error("jpg2rgb: %s", strerror(rc));
		goto finally;
	}

	if ((rc = load_file(out, rgb, size.width, size.height))) {
		error("load_file: %s", strerror(rc));
		unlink(rgb);
		goto finally;
	}

	black = contrast->lo.value;
	white = contrast->hi.value;

	if (contrast->lo.unit == '%' || contrast->hi.unit == '%') {
		if (!(histogram = histogram_new(256))) {
			rc = errno ? errno : -1;
			error("histogram_new: %s", strerror(rc));
			goto finally;
		}

		if ((rc = histogram_load(histogram, out, size.width,
				size.height))) {
			error("histogram_load: %s", strerror(rc));
			goto finally;
		}

		if (contrast->lo.unit == '%') {
			black = histogram_ratio_value(histogram,
					contrast->lo.value / 100);
		}

		if (contrast->hi.unit == '%') {
			white = histogram_ratio_value(histogram,
					contrast->hi.value / 100);
		}
	}

	fprintf(stdout, "%d:%d => ", black, white);

	if ((rc = rgb2jpg(output, quality, black, white, 1, 0, out,
			size.width, size.height))) {
		error("rgb2jpg: %s", strerror(rc));
		goto finally;
	}

	rc = 0;

finally:
	unlink(rgb);
	if (file) {
		fclose(file);
	}
	if (histogram) {
		histogram_free(histogram);
	}
	if (out) {
		free(out);
	}
	return rc;
}

int stretch(char *basename, int argc, char **argv)
{
	int rc, c, i, j;
	enum pit_log_level log_level = PIT_WARN;
	int quality;
	struct pit_range stretch, range;
	struct filelist list;
	struct file *item;
	size_t total, count;
	char *tmp, *output = NULL;
	char fmt[PATH_MAX];

	RB_INIT(&list);
	quality = DEFAULT_QUALITY;

	memset(&stretch, '\0', sizeof(stretch));
	stretch.lo.value = PIXEL_MIN;
	stretch.hi.value = PIXEL_MAX;

	memset(&range, '\0', sizeof(range));
	range.lo.value = -1;
	range.hi.value = -1;

	while ((c = getopt(argc, argv, "vq:o:c:t:")) != -1) {
		switch (c) {
		case 'v':
			log_level--;
			break;
		case 'q':
			quality = (int) strtol(optarg, &tmp, 10);

			if (*tmp != '\0' || quality < 0 || quality > 100) {
				rc = EINVAL;
				murmur("Invalid JPEG quality: %s\n", optarg);
				goto finally;
			}
			break;
		case 'o':
			output = optarg;
			break;
		case 'c':
			if ((rc = pit_range_parsef(&stretch, optarg)) ||
					stretch.lo.value < PIXEL_MIN ||
					stretch.hi.value > PIXEL_MAX ||
					(stretch.lo.unit != '\0' && stretch.lo.unit != '%') ||
					(stretch.hi.unit != '\0' && stretch.hi.unit != '%') ||
					(stretch.hi.unit == '%' && stretch.hi.value > 100)) {
				murmur("Invalid range of contrast stretch: %s\n", optarg);
				goto finally;
			}

			if (stretch.lo.unit == '%' && stretch.lo.value == 0) {
				stretch.lo.unit = 0;
				stretch.lo.value = PIXEL_MIN;
			}

			if (stretch.hi.unit == '%' && stretch.hi.value == 100) {
				stretch.lo.unit = 0;
				stretch.hi.value = PIXEL_MAX;
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
		if ((rc = filelist_list(&list, ".", &total, jpeg_filter,
				output))) {
			error("filelist_list: %s", strerror(rc));
			goto finally;
		}
	} else {
		total = 0;

		for (i = 0; i < argc; i++) {
			if (range.lo.value != -1 && range.hi.value != -1) {
				for (j = range.lo.value; j <= range.hi.value; j++) {
					snprintf(fmt, sizeof(fmt), argv[i], j);

					if ((rc = filelist_add(&list, fmt))) {
						if (rc == ENOENT) {
							fprintf(stderr, "no such file: %s", fmt);
							continue;
						} else if (rc == EEXIST) {
							fprintf(stderr, "exists: %s", fmt);
							continue;
						}

						error("filelist_add: %s", strerror(rc));
						goto finally;
					}

					total++;
				}
			} else {
				if ((rc = filelist_add(&list, argv[i]))) {
					if (rc == EEXIST) {
						continue;
					}

					error("filelist_add: %s", strerror(rc));
					goto finally;
				}

				total++;
			}
		}
	}

	if (RB_EMPTY(&list)) {
		murmur("No input file.\n");
		rc = EINVAL;
		goto finally;
	}

	if (output && total > 1) {
		murmur("Only one input was accepted if output specified.\n");
		rc = EINVAL;
		goto finally;
	}

	count = 0;

	RB_FOREACH(item, filelist, &list) {
		snprintf(fmt, sizeof(fmt), "%d", total);
		snprintf(fmt, sizeof(fmt), "%%0%dd", strlen(fmt));

		fprintf(stdout, fmt, count + 1);
		fprintf(stdout, "/%d: %s => ", total, item->path);

		if ((rc = stretch_file(item->path, &stretch, output,
				quality))) {
			error("stretch_file: %s", strerror(rc));
			goto finally;
		}

		fprintf(stdout, "OK\n");

		count++;

		if (count >= total) {
			break;
		}
	}

	rc = 0;

finally:
	filelist_clear(&list);
	return rc;
}
