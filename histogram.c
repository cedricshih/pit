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

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>

#include "log.h"

#include "histogram.h"

#define MAX(a,b) a > b ? a : b

struct histogram {
	size_t size;
	unsigned long total;
	unsigned int *values;
	unsigned int max;
	double *contribs;
	int dirty;
};

struct histogram *histogram_new(size_t size)
{
	int rc;
	struct histogram *histogram;

	if (!(histogram = calloc(1, sizeof(*histogram)))) {
		rc = errno ? errno : -1;
		error("calloc: %s", strerror(rc));
		goto finally;
	}

	histogram->size = size;

	if (!(histogram->values = calloc(size, sizeof(*histogram->values)))) {
		rc = errno ? errno : -1;
		error("calloc: %s", strerror(rc));
		goto finally;
	}

	if (!(histogram->contribs = calloc(size, sizeof(*histogram->contribs)))) {
		rc = errno ? errno : -1;
		error("calloc: %s", strerror(rc));
		goto finally;
	}

	rc = 0;

finally:
	if (rc != 0) {
		if (histogram) {
			histogram_free(histogram);
		}
		histogram = NULL;
		errno = rc;
	}
	return histogram;
}

void histogram_free(struct histogram *histogram)
{
	if (!histogram) {
		return;
	}

	if (histogram->contribs) {
		free(histogram->contribs);
	}

	if (histogram->values) {
		free(histogram->values);
	}

	free(histogram);
}

static inline int rgb2v(unsigned char *ptr)
{
	int v = (*ptr++ * 77);
	v += (*ptr++ * 151);
	v += (*ptr++ * 28);
	v >>= 8;

	return v;
}

int histogram_load(struct histogram *histogram, unsigned char *ptr,
		size_t w, size_t h)
{
	size_t y, i;
	int v;

	for (y = 0; y < h; y++) {
		for (i = 0; i < w; i++) {
			v = (*ptr++ * 77);
			v += (*ptr++ * 151);
			v += (*ptr++ * 28);
			v >>= 8;

			histogram->values[v]++;
			histogram->total++;
			histogram->max= MAX(histogram->max, histogram->values[v]);
			histogram->dirty = 1;
		}
	}

	return 0;
}

int histogram_load_file(struct histogram *histogram, const char *filename,
		size_t w, size_t h)
{
	int rc;
	FILE *file = NULL;
	unsigned char *buffer = NULL, *ptr;
	size_t s, y, n, i;
	unsigned int v;

	if (!(file = fopen(filename, "rb"))) {
		rc = errno ? errno : -1;
		error("fopen: %s", strerror(rc));
		goto finally;
	}

	s = w * 3;

	if (!(buffer = malloc(s))) {
		rc = errno ? errno : -1;
		error("malloc: %s", strerror(rc));
		goto finally;
	}

	for (y = 0; y < h; y++) {
		if ((n = fread(buffer, 1, s, file)) != s) {
			rc = errno ? errno : -1;
			error("fread: %s", strerror(rc));
			goto finally;
		}

		for (i = 0; i < w; i++) {
			ptr = buffer;

			v = (*ptr++ * 77);
			v += (*ptr++ * 151);
			v += (*ptr++ * 28);
			v >>= 8;

			histogram->values[v]++;
			histogram->total++;
			histogram->max= MAX(histogram->max, histogram->values[v]);
			histogram->dirty = 1;
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

size_t histogram_size(struct histogram *histogram)
{
	return histogram->size;
}

double histogram_contrib(struct histogram *histogram, size_t value)
{
	int i;

	if (histogram->dirty) {
		for (i = 0; i < histogram->size; i++) {
			histogram->contribs[i] = ((double) histogram->values[i]) / histogram->total;

			if (i != 0) {
				histogram->contribs[i] += histogram->contribs[i - 1];
			}
		}

		histogram->dirty = 0;
	}

	return histogram->contribs[value];
}

size_t histogram_ratio_value(struct histogram *histogram, float ratio)
{
	int i;
	double contrib;

	if (ratio <= 0) {
		return 0;
	}

	if (ratio >= 1) {
		return histogram->size - 1;
	}

	for (i = 0; i < histogram->size; i++) {
		contrib = histogram_contrib(histogram, i);

		if (contrib >= ratio) {
			return i;
		}
	}

	return histogram->size - 1;
}





