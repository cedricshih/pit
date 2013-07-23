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

#ifndef AVI_H_
#define AVI_H_

#include <sys/types.h>

#include "common.h"

#define avi_fourcc(b0,b1,b2,b3) (b0)|(b1<<8)|(b2<<16)|(b3<<24)

struct avi_writer;

struct avi_writer *avi_writer_new(u_int32_t fourcc, struct pit_dim *size,
		struct pit_frac *fps);

void avi_writer_free(struct avi_writer *writer);

struct video;

//int avi_writer_set_video(struct avi_writer *writer,
//		u_int32_t fourcc, const struct media *video,
//		struct dimension *size, struct fraction *fps);

int avi_writer_open(struct avi_writer *writer, const char *filename);

int avi_writer_close(struct avi_writer *writer);

int avi_writer_write(struct avi_writer *writer, void *data, size_t len);

size_t avi_writer_num_frames(struct avi_writer *writer);

//int avi_writer_stat(struct avi_writer *writer,
//		struct avi_writer_stat *stat);

//

enum {
	AVIF_HASINDEX = 0x00000010,
	AVIF_MUSTUSEINDEX = 0x00000020,
	AVIF_ISINTERLEAVED = 0x00000100,
	AVIF_COPYRIGHTED = 0x00010000,
	AVIF_WASCAPTUREFILE = 0x00020000
};

enum {
	AVISF_DISABLED = 0x00000001,
	AVISF_VIDEO_PALCHANGES = 0x00010000,
};

struct avi_hdr {
	u_int32_t us_per_frame;       // frame display rate (or 0); not really matter?
	u_int32_t max_bytes_per_sec;  // max. transfer rate; not really matter?
	u_int32_t padding;            // pad to multiples of this
	u_int32_t flags;              // the ever-present flags
	u_int32_t total_frames;       // total frames; not really matter?
	u_int32_t init_frames;        // ignore that
	u_int32_t streams;            // number of streams in the file
	u_int32_t suggested_buffer;   // size of buffer required to hold chunks; not really matter?
	u_int32_t width;              // width of video stream
	u_int32_t height;             // height of video stream
	u_int32_t reserved[4];
};

struct avi_rect {
	u_int16_t x;
	u_int16_t y;
	u_int16_t w;
	u_int16_t h;
};

struct avi_stream_hdr {
	u_int32_t type;                  // 'vids' = video, 'auds' = audio, 'txts' = subtitle
	u_int32_t handler;
	u_int32_t flags;
	u_int16_t priority;
	u_int16_t language;
	u_int32_t initialFrames;
	u_int32_t scale;
	u_int32_t rate;
	u_int32_t start;
	u_int32_t length;
	u_int32_t suggested_buffer;
	u_int32_t quality;
	u_int32_t sample_size;
	struct avi_rect frame;
};

struct avi_mjpg_stream {
	u_int32_t size;
	u_int32_t width;
	u_int32_t height;
	u_int16_t planes;
	u_int16_t bit_cnt;
	u_int32_t compression;
	u_int32_t image_size;
	u_int32_t xpels_meter;
	u_int32_t ypels_meter;
	u_int32_t num_colors;
	u_int32_t imp_colors;
};

struct avi_odm_hdr {
	u_int32_t total_frames;
};

enum {
	AVIIF_LIST = 0x00000001, /* chunk is a 'LIST' */
	AVIIF_TWOCC = 0x00000002,
	AVIIF_KEYFRAME = 0x00000010, /* this frame is a key frame. */
	AVIIF_FIRSTPART = 0x00000020,
	AVIIF_LASTPART = 0x00000040,
	AVIIF_MIDPART = (AVIIF_LASTPART | AVIIF_FIRSTPART),
	AVIIF_NOTIME = 0x00000100, /* this frame doesn't take any time */
	AVIIF_COMPUSE = 0x0FFF0000,
};

struct avi_index {
	u_int32_t type;
	u_int32_t flags;
	u_int32_t offset;
	u_int32_t size;
};

struct avi_writer {
	struct riff_tree *tree;
	char *filename;
	u_int32_t fourcc;
	struct pit_dim size;
	struct pit_frac fps;
	struct {
//		struct timeval start_time;
		size_t frames;
		size_t size;
	} stat;
	FILE *file;
	struct riff *avi;
	struct riff *avih;
	struct riff *strh;
	struct riff *movi;
};

//static int avi_writer_open(struct avi_writer *writer);
//
//static int avi_writer_close(struct avi_writer *writer);

//struct zcbuffer;
//
//static void avi_writer_video_cb(void *obj, enum media_event event,
//		const struct media_data *data, void *cbarg);

#endif /* AVI_H_ */
