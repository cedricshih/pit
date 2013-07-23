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
#include "filelist.h"
#include "jpg2rgb.h"
#include "rgb2jpg.h"

#define murmur(fmt...) fprintf(stderr, fmt)

#define DEFAULT_OUTOUT "startrails.jpg"
#define DEFAULT_QUALITY 98

#define PIXEL_MIN 0
#define PIXEL_MAX 255

void startrail_help(FILE *file, char *basename, char *cmd)
{
	fprintf(file, "Usage: %s %s [options] [file...]\n\n"
			"Options:\n"
			"    -o <output>         Output JPEG file. (default: %s)\n"
			"    -q <quality>        Output JPEG quality from 0 to 100 (default: %d)\n"
			"    -d <duration>       Maximum video duration. (unit: second)\n"
			"    -t <begin>:<end>    Treat file name as template, e.g. '%%08d.JPG'.\n"
			"\n", basename, cmd, DEFAULT_OUTOUT, DEFAULT_QUALITY);
}

static int parse_range(const char *arg, int *lo, int *hi)
{
	char *ptr;

	*lo = (int) strtol(arg, &ptr, 10);

	if (ptr == arg || *ptr != ':') {
		return EINVAL;
	}

	arg = ptr + 1;

	*hi = (int) strtol(arg, &ptr, 10);

	if (ptr == arg || *ptr != '\0') {
		return EINVAL;
	}

	return 0;
}

static int jpeg_filter(const char *filename, const char *extname, void *cbarg)
{
	if (extname && (!strcasecmp(extname, "jpg") ||
			!strcasecmp(extname, "jpeg"))) {
		return 1;
	} else {
		return 0;
	}
}

#define MAX(a,b) a > b ? a : b

int load_file(int *dst, const char *filename, size_t w, size_t h)
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
//			*dst++ += buffer[i];
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

int startrail(char *basename, char *cmd, int argc, char **argv)
{
	int rc, c, begin, end, i, j;
	enum pit_log_level log_level = PIT_WARN;
	int quality;
	size_t width, height, w, h, num;
	int *sum;
	unsigned char *out;
	struct filelist list;
	struct file *item;
	size_t total, count;
	char *output = DEFAULT_OUTOUT;
	char fmt[256];
	char rgb[PATH_MAX];
	struct stat st;
	off_t fsize;
	FILE *file;

	RB_INIT(&list);
	quality = DEFAULT_QUALITY;
	rgb[0] = '\0';

	begin = end = -1;
	width = height = 0;
	out = NULL;
	sum = NULL;
	file = NULL;

	while ((c = getopt(argc, argv, "vq:o:t:")) != -1) {
		switch (c) {
		case 'v':
			log_level--;
			break;
			break;
		case 'q':
			quality = (int) strtol(optarg, NULL, 10);

			if (quality < 0 || quality > 51) {
				rc = EINVAL;
				murmur("Invalid quality factor: %s\n", optarg);
				goto finally;
			}
			break;
		case 'o':
			output = optarg;
			break;
		case 't':
			if ((rc = parse_range(optarg, &begin, &end)) ||
					end < begin) {
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
		if ((rc = filelist_list(&list, ".", &total, jpeg_filter, NULL))) {
			error("filelist_list: %s", strerror(rc));
			goto finally;
		}
	} else {
		total = 0;

		for (i = 0; i < argc; i++) {
			if (begin != -1 && end != -1) {
				for (j = begin; j <= end; j++) {
					snprintf(rgb, sizeof(rgb), argv[i], j);

					if ((rc = filelist_add(&list, rgb))) {
						if (rc == ENOENT) {
							fprintf(stderr, "no such file: %s", rgb);
							continue;
						} else if (rc == EEXIST) {
							fprintf(stderr, "exists: %s", rgb);
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

	snprintf(rgb, sizeof(rgb), "decompressed.rgb");

	count = 0;

	RB_FOREACH(item, filelist, &list) {
		snprintf(fmt, sizeof(fmt), "%d", total);
		snprintf(fmt, sizeof(fmt), "%%0%dd", strlen(fmt));

		fprintf(stdout, fmt, count + 1);
		fprintf(stdout, "/%d: %s => ", total, item->path);

		if ((rc = jpg_read_header(item->path, &w, &h))) {
			error("jpg_read_header: %s", strerror(rc));
			goto finally;
		}

		if (!sum) {
			num = w * h * 3;

			if (!(sum = calloc(sizeof(*sum), num))) {
				error("malloc: %s", strerror(rc));
				goto finally;
			}

			width = w;
			height = h;
		} else {
			if (w != width || h != height) {
				fprintf(stdout, "Size mismatch: %dx%d\n",
						w, h);
				continue;
			}
		}

		if ((rc = jpg2rgb(item->path, rgb, 0, 255, 1, 0))) {
			error("jpg2rgb: %s", strerror(rc));
			goto finally;
		}

		if ((rc = load_file(sum, rgb, width, height))) {
			error("load_file: %s", strerror(rc));
			goto finally;
		}

		fprintf(stdout, "OK\n");

		count++;

		if (count >= total) {
			break;
		}
	}

	if (!(out = malloc(sizeof(*out) * num))) {
		error("malloc: %s", strerror(rc));
		goto finally;
	}

	for (i = 0; i < num; i++) {
//		out[i] = sum[i] / count;
		out[i] = sum[i];
	}

	if ((rc = rgb2jpg(output, quality, out, width, height))) {
		error("rgb2jpg: %s", strerror(rc));
		goto finally;
	}

	if ((rc = stat(output, &st))) {
		rc = errno ? errno : -1;
		error("stat: %s", strerror(rc));
		goto finally;
	}

	fprintf(stdout, "\nFinished: %s\n", output);
	fprintf(stdout, "Resolution: %dx%d\n", width, height);

	fsize = st.st_size;

	fprintf(stdout, "File Size: %ld bytes / %.2fMB\n", fsize,
			((float) fsize) / 1048576);
	rc = 0;

finally:
	if (file) {
		fclose(file);
	}
	if (out) {
		free(out);
	}
	if (sum) {
		free(sum);
	}
	filelist_clear(&list);
	return rc;
}
