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

#ifndef AVCENC_H_
#define AVCENC_H_

#include "common.h"

struct avcenc_session;

typedef void (*avcenc_session_cb)(struct avcenc_session *session,
		void *data, size_t len, void *cbarg);

struct avcenc_session *avcenc_session_new(const char *profile,
		struct pit_dim *size, struct pit_frac *frame_rate);

void avcenc_session_free(struct avcenc_session *session);

void avcenc_session_set_cb(struct avcenc_session *session,
		avcenc_session_cb cb, void *cbarg);

int avcenc_session_encode(struct avcenc_session *session,
		const unsigned char *data, const char *outfile);

int avcenc_session_pending_frames(struct avcenc_session *session);

int avcenc_session_flush(struct avcenc_session *session, const char *outfile);

#endif /* AVCENC_H_ */
