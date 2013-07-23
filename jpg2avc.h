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


#ifndef JPG2AVC_H_
#define JPG2AVC_H_

#include <sys/types.h>
#include <stdio.h>

#include "common.h"

struct jpg2avc;

struct jpg2avc *jpg2avc_new(struct pit_dim *size, struct pit_frac *frame_rate,
		int quality);

void jpg2avc_free(struct jpg2avc *ctx);

int jpg2avc_stretch_black(struct jpg2avc *ctx, unsigned int black);

int jpg2avc_stretch_black_ratio(struct jpg2avc *ctx, double black);

int jpg2avc_stretch_white(struct jpg2avc *ctx, unsigned int white);

int jpg2avc_stretch_white_ratio(struct jpg2avc *ctx, double white);

int jpg2avc_begin(struct jpg2avc *ctx, const char *output);

int jpg2avc_transcode(struct jpg2avc *ctx, const char *jpg, const char *rgb,
		const char *resized, const char *avc, double a, int b);

size_t jpg2avc_pending_frames(struct jpg2avc *ctx);

int jpg2avc_flush(struct jpg2avc *ctx, const char *avc);

int jpg2avc_commit(struct jpg2avc *ctx);

size_t jpg2avc_count(struct jpg2avc *ctx);

#endif /* JPG2AVC_H_ */
