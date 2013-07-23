// $Id$
/*
 * Copyright 2010 Cedric Shih (cedric dot shih at gmail dot com)
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

#include <sys/types.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <unistd.h>

#include "log.h"
#include "riff.h"

#include "avi.h"

static int avi_writer_init(struct avi_writer *writer);
static int avi_writer_finalize(struct avi_writer *writer);

struct avi_writer *avi_writer_new(u_int32_t fourcc, struct pit_dim *size,
		struct pit_frac *fps)
{
	struct avi_writer *writer;

	if (!(writer = calloc(1, sizeof(struct avi_writer)))) {
		error("failed to allocate for avi_writer");
		goto error;
	}

	memcpy(&writer->size, size, sizeof(writer->size));
	memcpy(&writer->fps, fps, sizeof(writer->fps));
	writer->fourcc = fourcc;

	return writer;
error:
	avi_writer_free(writer);
	return NULL;
}

void avi_writer_free(struct avi_writer *writer)
{
	if (!writer) {
		return;
	}

	if (writer->file) {
		avi_writer_close(writer);
	}

	if (writer->filename) {
		free(writer->filename);
	}

	if (writer->tree) {
		riff_tree_free(writer->tree);
	}

	free(writer);
}

//int avi_writer_set_video(struct avi_writer *writer,
//		u_int32_t fourcc, const struct media *video,
//		struct dimension *size, struct fraction *fps)
//{
//	int rc;
//
//	if (writer->stat.started) {
//		rc = EINPROGRESS;
//		error("already started");
//		goto finally;
//	}
//
//	if (!video || !video->ops || !video->obj) {
//		rc = EINVAL;
//		error("null argument(s)");
//		goto finally;
//	}
//
//	if (!video->ops->set_cb || !video->ops->unset_cb) {
//		rc = EINVAL;
//		error("no video callback support");
//		goto finally;
//	}
//
//	writer->fourcc = fourcc;
//	memcpy(&writer->video, video, sizeof(writer->video));
//	memcpy(&writer->size, size, sizeof(writer->size));
//	memcpy(&writer->fps, fps, sizeof(writer->fps));
//	rc = 0;
//
//finally:
//	return rc;
//}

int avi_writer_open(struct avi_writer *writer, const char *filename)
{
	int rc;

	if (!writer || !filename) {
		rc = EINVAL;
		error("null argument(s)");
		goto finally;
	}

	if (writer->file) {
		rc = EINPROGRESS;
		error("already open");
		goto finally;
	}

//	if (gettimeofday(&writer->stat.start_time, NULL) == -1) {
//		rc = errno ? errno : -1;
//		error("failed to get time: %s", strerror(rc));
//		goto finally;
//	}

	writer->stat.frames = 0;

	if (writer->filename) {
		free(writer->filename);
		writer->filename = NULL;
	}

	if (!(writer->filename = strdup(filename))) {
		rc = ENOMEM;
		error("failed to strdup: %s", filename);
		goto finally;
	}

	if (!(writer->file = fopen(writer->filename, "w+"))) {
		rc = errno ? errno : -1;
		error("failed to open file '%s': %s",
				writer->filename, strerror(rc));
		goto finally;
	}

	if ((rc = avi_writer_init(writer))) {
		error("failed to init: %s", strerror(rc));
		goto finally;
	}

	rc = 0;

finally:
	if (rc != 0) {
		if (writer->file) {
			avi_writer_close(writer);
		}
		if (writer->filename) {
			free(writer->filename);
			writer->filename = NULL;
		}
	}
	return rc;
}

int avi_writer_close(struct avi_writer *writer)
{
	int rc;

	if (!writer) {
		rc = EINVAL;
		error("null argument(s)");
		goto finally;
	}

	if (!writer->file) {
		rc = -1;
		error("not open");
		goto finally;
	}

	if ((rc = avi_writer_finalize(writer))) {
		error("failed to finalize: %s", strerror(rc));
		goto finally;
	}

	debug("closing file: %s", writer->filename);

	if (fflush(writer->file)) {
		rc = errno ? errno : -1;
		error("failed to flush file '%s': %s",
				writer->filename, strerror(rc));
		goto finally;
	}

	if (fclose(writer->file)) {
		rc = errno ? errno : -1;
		error("failed to close file '%s': %s",
				writer->filename, strerror(rc));
		goto finally;
	}

	writer->file = NULL;

	rc = 0;

finally:
	return rc;
}

//int avi_writer_stat(struct avi_writer *writer,
//		struct avi_writer_stat *stat)
//{
//	int rc;
//	struct riff_stat rstat;
//
//	if (!writer || !stat) {
//		rc = EINVAL;
//		error("null argument(s)");
//		goto finally;
//	}
//
//	if ((rc = riff_stat(writer->avi, &rstat))) {
//		error("failed to stat riff: %s", strerror(rc));
//		goto finally;
//	}
//
//	memcpy(stat, &writer->stat, sizeof(*stat));
//
//	if (stat->started) {
//		stat->size = rstat.size
//				+ riff_leaf_header_size()
//				+ sizeof(struct avi_index) * stat->frames;
//	}
//	rc = 0;
//
//finally:
//	return rc;
//}

int avi_writer_init(struct avi_writer *writer)
{
	int rc;
	struct riff *hdrl, *list, *leaf;
	union {
		struct avi_hdr avih;
		struct avi_stream_hdr strh;
		struct avi_mjpg_stream mjpg;
		struct avi_odm_hdr dmlh;
	} data;

	if (!(writer->tree = riff_tree_new(writer->file))) {
		rc = ENOMEM;
		error("failed to create RIFF tree");
		goto finally;
	}

	if ((rc = riff_tree_add_list(writer->tree,
			avi_fourcc('R', 'I', 'F', 'F'),
			avi_fourcc('A', 'V', 'I', ' '), &writer->avi))) {
		error("failed to add AVI RIFF-list: %s", strerror(rc));
		goto finally;
	}

	if ((rc = riff_add_list(writer->avi,
			avi_fourcc('L', 'I', 'S', 'T'),
			avi_fourcc('h', 'd', 'r', 'l'), &hdrl))) {
		error("failed to add hdrl list: %s", strerror(rc));
		goto finally;
	}

	if ((rc = riff_add_leaf(hdrl,
			avi_fourcc('a', 'v', 'i', 'h'),
			sizeof(data.avih), &writer->avih))) {
		error("failed to add AVI header chunk: %s", strerror(rc));
		goto finally;
	}

	memset(&data.avih, '\0', sizeof(data.avih));
	data.avih.us_per_frame = 1000000 * writer->fps.den
			/ writer->fps.num;
	data.avih.max_bytes_per_sec = writer->size.width * writer->size.height
			* 3 * writer->fps.num / writer->fps.den;
//	data.avih.flags = AVIF_WASCAPTUREFILE;
	data.avih.flags = AVIF_HASINDEX;
	data.avih.total_frames = writer->stat.frames;
	data.avih.streams = 1;
	data.avih.suggested_buffer = writer->size.width
			* writer->size.height * 3;
	data.avih.width = writer->size.width;
	data.avih.height = writer->size.height;

	if ((rc = riff_write(writer->avih, &data.avih,
			sizeof(data.avih)))) {
		error("failed to write AVI header chunk: %s",
				strerror(rc));
		goto finally;
	}

	if ((rc = riff_add_list(hdrl,
			avi_fourcc('L', 'I', 'S', 'T'),
			avi_fourcc('s', 't', 'r', 'l'), &list))) {
		error("failed to add strl list: %s", strerror(rc));
		goto finally;
	}

	if ((rc = riff_add_leaf(list,
			avi_fourcc('s', 't', 'r', 'h'),
			sizeof(data.strh), &writer->strh))) {
		error("failed to add stream header chunk: %s",
				strerror(rc));
		goto finally;
	}

	memset(&data.strh, '\0', sizeof(data.strh));
	data.strh.type = avi_fourcc('v', 'i', 'd', 's');
	data.strh.handler = writer->fourcc;
	data.strh.rate = writer->fps.num;
	data.strh.scale = writer->fps.den;
	data.strh.length = writer->stat.frames;
	data.strh.suggested_buffer = writer->size.width
			* writer->size.height * 3;
	data.strh.quality = -1;
	data.strh.frame.w = writer->size.width;
	data.strh.frame.h = writer->size.height;

	if ((rc = riff_write(writer->strh, &data.strh,
			sizeof(data.strh)))) {
		error("failed to write stream header chunk: %s",
				strerror(rc));
		goto finally;
	}

	if ((rc = riff_add_leaf(list,
			avi_fourcc('s', 't', 'r', 'f'),
			sizeof(data.mjpg), &leaf))) {
		error("failed to add stream chunk: %s",
				strerror(rc));
		goto finally;
	}

	memset(&data.mjpg, '\0', sizeof(data.mjpg));
	data.mjpg.size = sizeof(data.mjpg);
	data.mjpg.width = writer->size.width;
	data.mjpg.height = writer->size.height;
	data.mjpg.planes = 1; // what is this?
	data.mjpg.bit_cnt = 24; // what is this?
	data.mjpg.compression = writer->fourcc;
	data.mjpg.image_size = writer->size.width * writer->size.height * 3;

	if ((rc = riff_write(leaf, &data.mjpg, sizeof(data.mjpg)))) {
		error("failed to write MJPEG stream chunk: %s",
				strerror(rc));
		goto finally;
	}

	if ((rc = riff_add_list(list,
			avi_fourcc('L', 'I', 'S', 'T'),
			avi_fourcc('o', 'd', 'm', 'l'), &list))) {
		error("failed to add stream chunk: %s",
				strerror(rc));
		goto finally;
	}

	if ((rc = riff_add_leaf(list,
			avi_fourcc('d', 'm', 'l', 'h'),
			sizeof(data.dmlh), &leaf))) {
		error("failed to add stream chunk: %s",
				strerror(rc));
		goto finally;
	}

	memset(&data.dmlh, '\0', sizeof(data.dmlh));

	if ((rc = riff_write(leaf, &data.dmlh, sizeof(data.dmlh)))) {
		error("failed to write Open-DMX extended AVI header "
				"chunk: %s", strerror(rc));
		goto finally;
	}

	if ((rc = riff_add_list(writer->avi,
			avi_fourcc('L', 'I', 'S', 'T'),
			avi_fourcc('m', 'o', 'v', 'i'), &writer->movi))) {
		error("failed to add movi list: %s", strerror(rc));
		goto finally;
	}

finally:
	if (rc != 0) {
		if (writer->tree) {
			riff_tree_free(writer->tree);
			writer->tree = NULL;
		}
	}
	return rc;
}

int avi_writer_finalize(struct avi_writer *writer)
{
	int rc;
//	struct timeval tv;
//	double elapse, fps;
	union {
		struct avi_hdr avih;
		struct avi_stream_hdr strh;
	} data;
	struct riff_list *list;
	struct riff *idx1;
	struct riff_item *frame;
	struct riff_stat stat;
	struct avi_index idx;

	debug("finalizing: %s", writer->filename);

//	if (gettimeofday(&tv, NULL) == -1) {
//		rc = errno ? errno : -1;
//		error("failed to get time: %s", strerror(rc));
//		goto finally;
//	}

//	if (tv.tv_usec < writer->stat.start_time.tv_usec) {
//		tv.tv_sec--;
//		tv.tv_usec += 1000000;
//	}

//	elapse = tv.tv_sec - writer->stat.start_time.tv_sec
//			+ (tv.tv_usec - writer->stat.start_time.tv_usec)
//			/ 1000000.0;
//	fps = writer->stat.frames / elapse;
//
//	error("elapsed: %f, frames: %d, fps: %f", elapse, writer->stat.frames, elapse);

	memset(&data.avih, '\0', sizeof(data.avih));
//	data.avih.us_per_frame = (1000000
//			* (tv.tv_sec - writer->stat.start_time.tv_sec)
//			+ (tv.tv_usec - writer->stat.start_time.tv_usec))
//			/ writer->stat.frames;
//	data.avih.max_bytes_per_sec = writer->size.width * writer->size.height
//			* 3 * fps;
	data.avih.us_per_frame = 1000000 * writer->fps.den
			/ writer->fps.num;
	data.avih.max_bytes_per_sec = writer->size.width * writer->size.height
			* 3 * writer->fps.num / writer->fps.den;
//	data.avih.flags = AVIF_WASCAPTUREFILE;
	data.avih.flags = AVIF_HASINDEX;
	data.avih.total_frames = writer->stat.frames;
//	data.avih.init_frames = 0;
	data.avih.streams = 1;
	data.avih.suggested_buffer = writer->size.width
			* writer->size.height * 3;
	data.avih.width = writer->size.width;
	data.avih.height = writer->size.height;

	if ((rc = riff_update(writer->avih, &data.avih,
			sizeof(data.avih)))) {
		error("failed to update AVI header chunk: %s",
				strerror(rc));
		goto finally;
	}

	memset(&data.strh, '\0', sizeof(data.strh));
	data.strh.type = avi_fourcc('v', 'i', 'd', 's');
	data.strh.handler = writer->fourcc;
//	data.strh.rate = fps;
//	data.strh.scale = 1;
	data.strh.rate = writer->fps.num;
	data.strh.scale = writer->fps.den;
	data.strh.length = writer->stat.frames;
	data.strh.suggested_buffer = writer->size.width
			* writer->size.height * 3;
	data.strh.quality = -1;
	data.strh.frame.w = writer->size.width;
	data.strh.frame.h = writer->size.height;

	if ((rc = riff_update(writer->strh, &data.strh,
			sizeof(data.strh)))) {
		error("failed to update stream header chunk: %s",
				strerror(rc));
		goto finally;
	}

	if ((rc = riff_add_leaf(writer->avi,
			avi_fourcc('i', 'd', 'x', '1'),
			sizeof(idx) * writer->stat.frames, &idx1))) {
		error("failed to add index chunk: %s",
				strerror(rc));
		goto finally;
	}

	list = riff_iterator(writer->movi);

	TAILQ_FOREACH(frame, list, next) {
		if ((rc = riff_stat(frame->riff, &stat))) {
			error("failed to stat riff: %s", strerror(rc));
			goto finally;
		}

		idx.type = avi_fourcc('0', '0', 'd', 'c');
		idx.flags = AVIIF_KEYFRAME | AVIIF_TWOCC;
		idx.offset = stat.offset; // - writer->movi->offset;
		idx.size = stat.size;

		if ((rc = riff_write(idx1, &idx, sizeof(idx)))) {
			error("failed to write index: %s", strerror(rc));
			goto finally;
		}
	}

	if ((rc = riff_tree_refresh(writer->tree))) {
		error("failed to refresh RIFF: %s", strerror(rc));
		goto finally;
	}

	riff_tree_free(writer->tree);
	writer->tree = NULL;
	rc = 0;

finally:
	return rc;
}

int avi_writer_write(struct avi_writer *writer, void *data, size_t len)
{
	int rc;
	size_t align;
	unsigned char padding[4];
	struct riff *leaf;

	align = sizeof(u_int32_t) - (len % sizeof(u_int32_t));

	if ((rc = riff_add_leaf(writer->movi,
			avi_fourcc('0', '0', 'd', 'c'),
			len + align, &leaf))) {
		error("failed to add stream chunk: %s",
				strerror(rc));
		goto finally;
	}

	if ((rc = riff_write(leaf, data, len))) {
		error("failed to write frame chunk: %s",
				strerror(rc));
		goto finally;
	}

	if (align > 0) {
		memset(padding, 0xff, sizeof(padding));

		if ((rc = riff_write(leaf, padding, align))) {
			error("failed to align frame chunk: %s",
					strerror(rc));
			goto finally;
		}
	}

	writer->stat.frames++;
	rc = 0;

finally:
	return rc;
}

size_t avi_writer_num_frames(struct avi_writer *writer)
{
	return writer->stat.frames;
}
