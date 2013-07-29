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
#include <string.h>
#include <errno.h>
#include <stdint.h>

#include <x264.h>

#include "log.h"

#include "avcenc.h"

struct avcenc_session {
        struct {
                x264_param_t param;
                x264_t *encoder;
                int64_t pts;
        } x264;
        avcenc_session_cb cb;
        void *cbarg;
};

void pit_x264_logger(void *priv, int level, const char *fmt, va_list ap)
{
        enum pit_log_level lv;

        switch (level) {
        case X264_LOG_ERROR:
                lv = PIT_ERROR;
                break;
        case X264_LOG_WARNING:
                lv = PIT_WARN;
                break;
        case X264_LOG_INFO:
                lv = PIT_DEBUG;
                break;
        case X264_LOG_DEBUG:
                lv = PIT_TRACE;
                break;
        default:
                lv = PIT_FATAL;
                break;
        }

        pit_vlog(lv, __func__, 0, fmt, ap);
}

struct avcenc_session *avcenc_session_new(const char *profile,
		struct pit_dim *size, struct pit_frac *frame_rate)
{
	int rc;
	struct avcenc_session *session = NULL;
	x264_param_t *param;

	if (!(session = calloc(1, sizeof(*session)))) {
		rc = errno ? errno : -1;
		error("calloc: %s", strerror(rc));
		goto finally;
	}

	param = &session->x264.param;

	debug("applying default param");

	x264_param_default(param);

	if ((rc = x264_param_default_preset(param, "veryslow", "film"))) {
		warn("failed to apply preset: %s", strerror(rc));
	}

	param->i_csp = X264_CSP_I420;
	param->i_width = size->width;
	param->i_height = size->height;
	param->i_fps_num = param->i_timebase_den = frame_rate->num;
	param->i_fps_den = param->i_timebase_num = frame_rate->den;

	param->b_annexb = 1;
	param->i_threads = 8;

//        param->rc.i_lookahead = 0;
//        param->i_sync_lookahead = 0;
//        param->i_bframe = 0;
//        param->b_sliced_threads = 1;
//        param->b_vfr_input = 0;
//        param->rc.b_mb_tree = 0;

//        param->i_keyint_max = frame_rate->num * 10 / frame_rate->den;

//        param->rc.i_rc_method = X264_RC_CQP;
//        param->rc.i_qp_constant = quality;

	if ((rc = x264_param_apply_profile(param, profile))) {
		warn("failed to apply profile '%s': %s", profile, strerror(rc));
	}

	/* log redirect */

	param->pf_log = pit_x264_logger;
	param->i_log_level = X264_LOG_DEBUG;

	if (!(session->x264.encoder = x264_encoder_open(param))) {
		rc = errno ? errno : -1;
		error("x264_encoder_open: %s", strerror(rc));
		goto finally;
	}

	rc = 0;

finally:
	if (rc != 0) {
		if (!session) {
			avcenc_session_free(session);
			session = NULL;
		}
	}
	return session;
}

void avcenc_session_free(struct avcenc_session *session)
{
	if (!session) {
		return;
	}

        if (session->x264.encoder) {
                x264_encoder_close(session->x264.encoder);
        }

	free(session);
}

void avcenc_session_set_cb(struct avcenc_session *session,
		avcenc_session_cb cb, void *cbarg)
{
	session->cb = cb;
	session->cbarg = cbarg;
}

int avcenc_session_encode(struct avcenc_session *session,
		const unsigned char *data, const char *outfile)
{
        int rc;
        x264_nal_t *nals, *nal = NULL;
        int num_nals, i;
        x264_picture_t input, output;
        FILE *file = NULL;
        size_t n;

        if (!session || !data) {
                rc = EINVAL;
                error("null pointer");
                goto finally;
        }

        trace("encoding %p", data);

        x264_picture_init(&input);
        x264_picture_init(&output);
//      memset(&input, '\0', sizeof(input));
//      memset(&output, '\0', sizeof(output));

        input.i_type = X264_TYPE_AUTO;
        input.i_qpplus1 = 0;
        input.img.i_csp = session->x264.param.i_csp;
        input.img.i_plane = 3;
        input.img.plane[0] = (unsigned char *) data;
        input.img.plane[1] = input.img.plane[0] + session->x264.param.i_width *
        		session->x264.param.i_height;
        input.img.plane[2] = input.img.plane[1] + session->x264.param.i_width *
                        session->x264.param.i_height / 4;
        input.img.i_stride[0] = session->x264.param.i_width;
        input.img.i_stride[1] = session->x264.param.i_width / 2;
        input.img.i_stride[2] = session->x264.param.i_width / 2;
        input.param = NULL;
        input.i_pts = session->x264.pts++;

        if (x264_encoder_encode(session->x264.encoder, &nals, &num_nals,
                        &input, &output) < 0) {
                rc = errno ? errno : -1;
                error("failed to encode: %s", strerror(rc));
                goto finally;
        }

        if (num_nals != 0) {
                if (!(file = fopen(outfile, "w+"))) {
                        rc = errno ? errno : -1;
                        error("fopen: %s", strerror(rc));
                        goto finally;
                }

                trace("writing %d NALUs to %s", num_nals, outfile);

                for (i = 0; i < num_nals; i++) {
                        nal = nals + i;

                        if (session->cb) {
                        	(*session->cb)(session, nal->p_payload,
                        			nal->i_payload, session->cbarg);
                        }

                        trace("writing nal[%d]: %d", i, 0x1f & *(nal->p_payload + 4));

                        if ((n = fwrite(nal->p_payload, 1, nal->i_payload,
                        		file)) != nal->i_payload) {
                        	rc = errno ? errno : -1;
                        	error("fwrite: %s", strerror(rc));
                        	goto finally;
                        }
                }
                rc = 0;
        } else {
        	rc = EAGAIN;
        }

finally:
	if (file) {
		fclose(file);
	}
        return rc;
}

int avcenc_session_pending_frames(struct avcenc_session *session)
{
	return x264_encoder_delayed_frames(session->x264.encoder);
}

int avcenc_session_flush(struct avcenc_session *session, const char *outfile)
{
        int rc;
        x264_nal_t *nals, *nal = NULL;
        int num_nals, i;
        x264_picture_t output;
        FILE *file = NULL;
        size_t n;

        if (!session) {
                rc = EINVAL;
                error("null pointer");
                goto finally;
        }

        x264_picture_init(&output);

	if (x264_encoder_encode(session->x264.encoder, &nals, &num_nals,
			NULL, &output) < 0) {
		rc = errno ? errno : -1;
		error("failed to encode: %s", strerror(rc));
		goto finally;
	}

	if (num_nals != 0) {
		if (!(file = fopen(outfile, "w+"))) {
			rc = errno ? errno : -1;
			error("fopen: %s", strerror(rc));
			goto finally;
		}

		trace("writing %d NALUs to %s", num_nals, outfile);

		for (i = 0; i < num_nals; i++) {
			nal = nals + i;

			if (session->cb) {
				(*session->cb)(session, nal->p_payload,
						nal->i_payload, session->cbarg);
			}

			trace("writing nal[%d]: %d", i, 0x1f & *(nal->p_payload + 4));

			if ((n = fwrite(nal->p_payload, 1, nal->i_payload,
					file)) != nal->i_payload) {
				rc = errno ? errno : -1;
				error("fwrite: %s", strerror(rc));
				goto finally;
			}
		}
	        rc = 0;
       } else {
        	rc = EAGAIN;
	}

finally:
	if (file) {
		fclose(file);
	}
        return rc;
}
