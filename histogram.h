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


#ifndef HISTOGRAM_H_
#define HISTOGRAM_H_

struct histogram;

struct histogram *histogram_new(size_t size);

void histogram_free(struct histogram *histogram);

int histogram_load(struct histogram *histogram, unsigned char *rgb,
		size_t stride, size_t scanline);

int histogram_load_file(struct histogram *histogram, const char *file,
		size_t stride, size_t scanline);

size_t histogram_size(struct histogram *histogram);

double histogram_contrib(struct histogram *histogram, size_t value);

size_t histogram_ratio_value(struct histogram *histogram, float ratio);

#endif /* HISTOGRAM_H_ */
