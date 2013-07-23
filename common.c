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

#include <errno.h>
#include <stdlib.h>

#include "common.h"

int pit_dim_parse(struct pit_dim *dim, const char *str)
{
	char *ptr;

	dim->width = (int) strtol(str, &ptr, 10);

	if (ptr == str || *ptr != 'x') {
		return EINVAL;
	}

	dim->height = (int) strtol(ptr + 1, &ptr, 10);

	if (ptr == str || *ptr != '\0') {
		return EINVAL;
	}

	return 0;
}

int pit_range_parsef(struct pit_range *range, const char *str)
{
	char *ptr;

	range->lo.value = strtof(str, &ptr);

	if (ptr == str || *ptr == '\0') {
		return EINVAL;
	}

	if (*ptr != ':') {
		if (*(ptr + 1) != ':') {
			return EINVAL;
		}

		range->lo.unit = *ptr++;
	} else {
		range->lo.unit = '\0';
	}

	str = ptr + 1;

	range->hi.value = strtof(str, &ptr);

	if (ptr == str) {
		return EINVAL;
	}

	if (*ptr != '\0') {
		if (*(ptr + 1) != '\0') {
			return EINVAL;
		}

		range->hi.unit = *ptr;
	} else {
		range->hi.unit = '\0';
	}

	if (range->lo.unit == range->hi.unit &&
			range->lo.value > range->hi.value) {
		return EINVAL;
	}

	return 0;
}

int pit_range_parse(struct pit_range *range, const char *str)
{
	char *ptr;

	range->lo.value = strtol(str, &ptr, 10);

	if (ptr == str || *ptr == '\0') {
		return EINVAL;
	}

	if (*ptr != ':') {
		if (*(ptr + 1) != ':') {
			return EINVAL;
		}

		range->lo.unit = *ptr++;
	} else {
		range->lo.unit = '\0';
	}

	str = ptr + 1;

	range->hi.value = strtol(str, &ptr, 10);

	if (ptr == str) {
		return EINVAL;
	}

	if (*ptr != '\0') {
		if (*(ptr + 1) != '\0') {
			return EINVAL;
		}

		range->hi.unit = *ptr;
	} else {
		range->hi.unit = '\0';
	}

	if (range->lo.unit == range->hi.unit &&
			range->lo.value > range->hi.value) {
		return EINVAL;
	}

	return 0;
}
