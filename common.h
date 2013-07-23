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


#ifndef COMMON_H_
#define COMMON_H_

#include <sys/types.h>

struct pit_dim {
	size_t width;
	size_t height;
};

int pit_dim_parse(struct pit_dim *dim, const char *str);

struct pit_frac {
	int num;
	int den;
};

struct pit_range {
	struct {
		float value;
		char unit;
	} lo, hi;
};

int pit_range_parsef(struct pit_range *range, const char *str);

int pit_range_parse(struct pit_range *range, const char *str);

#endif /* COMMON_H_ */
