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
#include "jpg2avc.h"

#define murmur(fmt...) fprintf(stderr, fmt)

#define DEFAULT_OUTOUT "timelapse.avi"
#define DEFAULT_FPS 24
#define DEFAULT_QUALITY 10

#define PIXEL_MIN 0
#define PIXEL_MAX 255

void timelapse_help(FILE *file, char *basename, char *cmd)
{
	fprintf(file, "Usage: %s %s [options] <width>x<height> [file...]\n\n"
			"Options:\n"
			"    -q <quality>        Quality from 0 to 51; 0 = lossless (default: %d)\n"
			"    -o <output>         Output video file. (default: %s)\n"
			"    -f <fps>            Video frame rate. (default: %d)\n"
			"    -d <duration>       Maximum video duration. (unit: second)\n"
			"    -s <black>[:white]  Stretch contrast; black and white points could be pixel value or percentage calculated from first frame.\n"
			"    -t <begin>:<end>    Treat file name as template, e.g. '%%08d.JPG'.\n"
			"    -F <head>:<tail>    Fade in/out effect. (unit: second)\n"
			"\n", basename, cmd, DEFAULT_QUALITY, DEFAULT_OUTOUT,
			DEFAULT_FPS);
}

static int parse_rangef(const char *arg, float *lo, char *lo_unit,
		float *hi, char *hi_unit)
{
	char *ptr;

	*lo = strtof(arg, &ptr);

	if (ptr == arg || *ptr == '\0') {
		return EINVAL;
	}

	if (*ptr != ':') {
		if (*(ptr + 1) != ':') {
			return EINVAL;
		}

		*lo_unit = *ptr++;
	} else {
		*lo_unit = '\0';
	}

	arg = ptr + 1;

	*hi = strtof(arg, &ptr);

	if (ptr == arg) {
		return EINVAL;
	}

	if (*ptr != '\0') {
		if (*(ptr + 1) != '\0') {
			return EINVAL;
		}

		*hi_unit = *ptr;
	} else {
		*hi_unit = '\0';
	}

	return 0;
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

static int parse_size(const char *arg, size_t *w, size_t *h)
{
	char *ptr;

	*w = (int) strtol(arg, &ptr, 10);

	if (ptr == arg || *ptr != 'x') {
		return EINVAL;
	}

	*h = (int) strtol(ptr + 1, &ptr, 10);

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

int timelapse(char *basename, char *cmd, int argc, char **argv)
{
	int rc, c, begin, end, i, j;
	enum pit_log_level log_level = PIT_WARN;
	struct avdim size;
	struct avfrac frame_rate;
	int duration;
	int quality;
	float lo, hi;
	char lo_unit, hi_unit;
	int fade_in, fade_out;
	struct filelist list;
	struct file *item;
	size_t total, limit, current;
	char *output = DEFAULT_OUTOUT;
	char fmt[256];
	char rgb[PATH_MAX];
	char resized[PATH_MAX];
	char avc[PATH_MAX];
	int *fades = NULL;
	struct jpg2avc *ctx = NULL;
	int num;
	double step;
	long msec;
	long sec;
	long min;
	long hour;
	struct stat st;
	off_t fsize;

	RB_INIT(&list);
	frame_rate.num = DEFAULT_FPS;
	frame_rate.den = 1;
	duration = 0;
	quality = DEFAULT_QUALITY;
	rgb[0] = '\0';
	resized[0] = '\0';
	avc[0] = '\0';
	fade_in = fade_out = 0;

	lo = PIXEL_MIN;
	hi = PIXEL_MAX;
	begin = end = -1;

	while ((c = getopt(argc, argv, "vd:q:o:s:f:t:F:")) != -1) {
		switch (c) {
		case 'v':
			log_level--;
			break;
		case 'd':
			duration = (int) strtol(optarg, NULL, 10);
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
		case 's':
			if ((rc = parse_rangef(optarg, &lo, &lo_unit, &hi, &hi_unit))
					|| lo < PIXEL_MIN || hi > PIXEL_MAX ||
					(lo_unit != '\0' && lo_unit != '%') ||
					(hi_unit != '\0' && hi_unit != '%') ||
					(hi_unit == '%' && hi > 100)) {
				murmur("Invalid range of contrast stretch: %s\n", optarg);
				goto finally;
			}

			if (lo_unit == '%' && lo == 0) {
				lo_unit = 0;
				lo = PIXEL_MIN;
			}

			if (hi_unit == '%' && hi == 100) {
				lo_unit = 0;
				hi = PIXEL_MAX;
			}
			break;
		case 'f':
			frame_rate.num = strtol(optarg, NULL, 10);
			break;
		case 't':
			if ((rc = parse_range(optarg, &begin, &end)) ||
					end < begin) {
				murmur("Invalid range of template: %s\n", optarg);
				goto finally;
			}
			break;
		case 'F':
			if ((rc = parse_range(optarg, &fade_in, &fade_out))) {
				murmur("Invalid range of fade in/out: %s\n",
						optarg);
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

	if (argc < 1) {
		timelapse_help(stderr, basename, cmd);
		return EINVAL;
	}

	pit_set_log_level(log_level);

	if ((rc = parse_size(argv[0], &size.width, &size.height))) {
		murmur("Invalid size: %s\n", argv[0]);
		goto finally;
	}

	if (size.width % 8 != 0 || size.height % 8 != 0) {
		murmur("Invalid size: %s; must be multiples of 8.\n", argv[0]);
		goto finally;
	}

	if (argc == 1) {
		if ((rc = filelist_list(&list, ".", &total, jpeg_filter, NULL))) {
			error("filelist_list: %s", strerror(rc));
			goto finally;
		}
	} else {
		total = 0;

		for (i = 1; i < argc; i++) {
			if (begin != -1 && end != -1) {
				for (j = begin; j <= end; j++) {
					snprintf(rgb, sizeof(rgb), argv[i], j);

					if ((rc = filelist_add(&list, rgb))) {
						if (rc == ENOENT || rc == EEXIST) {
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
	snprintf(resized, sizeof(resized), "resized.rgb");
	snprintf(avc, sizeof(avc), "encoded.264");

	if (!(ctx = jpg2avc_new(&size, &frame_rate, quality))) {
		rc = errno ? errno : -1;
		error("jpg2avc_new: %s", strerror(rc));
		goto finally;
	}

	if (lo_unit == '%') {
		if ((rc = jpg2avc_stretch_black_ratio(ctx, lo / 100))) {
			error("jpg2avc_stretch_black_ratio: %s", strerror(rc));
			goto finally;
		}
	} else {
		if ((rc = jpg2avc_stretch_black(ctx, (int) lo))) {
			error("jpg2avc_stretch_black: %s", strerror(rc));
			goto finally;
		}
	}

	if (hi_unit == '%') {
		if ((rc = jpg2avc_stretch_white_ratio(ctx, hi / 100))) {
			error("jpg2avc_stretch_white_ratio: %s", strerror(rc));
			goto finally;
		}
	} else {
		if ((rc = jpg2avc_stretch_white(ctx, (int) hi))) {
			error("jpg2avc_stretch_white: %s", strerror(rc));
			goto finally;
		}
	}

	if ((rc = jpg2avc_begin(ctx, output))) {
		error("jpg2avc_begin: %s", strerror(rc));
		goto finally;
	}

	limit = duration * frame_rate.num / frame_rate.den;
	total = (limit != 0 && total > limit) ? limit : total;

	if (!(fades = calloc(total, sizeof(*fades)))) {
		rc = errno ? errno : -1;
		error("calloc: %s", strerror(rc));
		goto finally;
	}

	if (fade_in != 0) {
		num = fade_in * frame_rate.num / frame_rate.den;

		if (num + fade_out * frame_rate.num / frame_rate.den < total) {
			step = ((float) PIXEL_MAX) / num;

			for (i = 0; i < num; i++) {
				fades[i] = (i - num) * step;
			}
		} else {
			murmur("Duration too short for fade in/out.\n");
			rc = EINVAL;
			goto finally;
		}
	}

	if (fade_out != 0) {
		num = fade_out * frame_rate.num / frame_rate.den;

		if (num + fade_in * frame_rate.num / frame_rate.den < total) {
			step = ((float) PIXEL_MAX) / num;

			for (i = 0; i < num; i++) {
				fades[total - num + i] = -i * step;
			}
		} else {
			murmur("Duration too short for fade in/out.\n");
			rc = EINVAL;
			goto finally;
		}
	}

	current = 0;

	RB_FOREACH(item, filelist, &list) {
		snprintf(fmt, sizeof(fmt), "%d", total);
		snprintf(fmt, sizeof(fmt), "%%0%dd", strlen(fmt));

		fprintf(stdout, fmt, current + 1);
		fprintf(stdout, "/%d: %s => ", total, item->path);

		if ((rc = jpg2avc_transcode(ctx, item->path, rgb, resized, avc,
				1.0, fades[current++]))) {
			if ((rc == EINVAL)) {
				fprintf(stdout, "Invalid JPEG or aspect ratio\n");
				continue;
			} else {
				error("jpg2avc_transcode: %s", strerror(rc));
				goto finally;
			}
		}

		fprintf(stdout, "OK\n");

		if (current >= total) {
			break;
		}
	}

	total = jpg2avc_pending_frames(ctx);
	current = 0;

	if (total > 0) {
		fprintf(stdout, "\nPASS 2: %d frames\n\n", total);
	}

	while (jpg2avc_pending_frames(ctx) > 0) {
		snprintf(fmt, sizeof(fmt), "%d", total);
		snprintf(fmt, sizeof(fmt), "%%0%dd", strlen(fmt));

		fprintf(stdout, fmt, current + 1);
		fprintf(stdout, "/%d: ", total);

		if ((rc = jpg2avc_flush(ctx, avc))) {
			if (rc != EAGAIN) {
				error("jpg2avc_flush: %s", strerror(rc));
				goto finally;
			}
		}

		if (rc == EAGAIN) {
			fprintf(stdout, "Pending\n");
		} else {
			fprintf(stdout, "OK\n");
			current++;
		}
	}

	if ((rc = jpg2avc_commit(ctx))) {
		error("jpg2avc_commit: %s", strerror(rc));
		goto finally;
	}

	if ((rc = stat(output, &st))) {
		rc = errno ? errno : -1;
		error("stat: %s", strerror(rc));
		goto finally;
	}

	msec = 1000 * jpg2avc_count(ctx) * frame_rate.den / frame_rate.num;

	sec = msec / 1000;
	msec %= 1000;

	min = sec / 60;
	sec %= 60;

	hour = sec / 60;
	min %= 60;

	fprintf(stdout, "\nFinished: %s\n", output);
	fprintf(stdout, "Resolution: %dx%d\n", size.width, size.height);
	fprintf(stdout, "Frame Rate: %.2f\n", (float) frame_rate.num / frame_rate.den);
	fprintf(stdout, "Duration: %02ld:%02ld:%02ld.%03ld\n",
			hour, min, sec, msec);

	fsize = st.st_size;

	fprintf(stdout, "File Size: %ld bytes / %.2fMB\n", fsize,
			((float) fsize) / 1048576);
	fprintf(stdout, "Average Bit Rate: %.2f Mbps\n",
			(float) fsize * 8 / jpg2avc_count(ctx) /
			frame_rate.den * frame_rate.num / 1000000);
	rc = 0;

finally:
	if (fades) {
		free(fades);
	}
	if (ctx) {
		jpg2avc_free(ctx);
	}
	filelist_clear(&list);
	return rc;
}
