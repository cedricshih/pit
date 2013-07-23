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

#include <stdlib.h>
#include <errno.h>
#include <string.h>

#include "log.h"

#include "riff.h"

struct riff_tree *riff_tree_new(FILE *file)
{
	struct riff_tree *tree;

	if (!(tree = calloc(1, sizeof(*tree)))) {
		error("failed to calloc riff_tree");
		goto error;
	}

	TAILQ_INIT(&tree->list);
	tree->file = file;

	return tree;

error:
	riff_tree_free(tree);
	return NULL;
}

void riff_tree_free(struct riff_tree *tree)
{
	if (!tree) {
		return;
	}

	riff_list_clear(&tree->list);

	free(tree);
}

int riff_tree_add_list(struct riff_tree *tree,
		u_int32_t type, u_int32_t subtype, struct riff **child)
{
	int rc;

	if ((rc = riff_list_add_list(&tree->list, NULL, tree->file,
			type, subtype, child))) {
		goto finally;
	}

	rc = 0;

finally:
	return rc;
}

int riff_tree_add_leaf(struct riff_tree *tree,
		u_int32_t type, u_int32_t size, struct riff **child)
{
	int rc;

	if ((rc = riff_list_add_leaf(&tree->list, NULL, tree->file,
			type, size, child))) {
		goto finally;
	}

	rc = 0;

finally:
	return rc;
}

int riff_tree_refresh(struct riff_tree *tree)
{
	int rc;

	debug("refreshing headers");

	if ((rc = riff_list_write_header(&tree->list))) {
		goto finally;
	}

finally:
	return rc;
}

int riff_list_add_list(struct riff_list *list,
		struct riff *parent, FILE *file,
		u_int32_t type, u_int32_t subtype, struct riff **out)
{
	int rc;
	struct riff *riff = NULL;
	struct riff_item *item = NULL;

	debug("adding list chunk");

	if (!(item = calloc(1, sizeof(*item)))) {
		rc = ENOMEM;
		error("failed to allocate riff_item");
		goto finally;
	}

	if (!(riff = calloc(1, sizeof(*riff)))) {
		rc = ENOMEM;
		error("failed to allocate riff");
		goto finally;
	}

	TAILQ_INIT(&riff->list);

	riff->parent = parent;
	riff->file = file;
	riff->type = FALCO_RIFF_LIST;
	riff->offset = -1;
	riff->header.list.type = type;
	riff->header.list.size = riff->size = sizeof(riff->header.list.subtype);
	riff->header.list.subtype = subtype;
	riff->name[0] = type & 0xff;
	riff->name[1] = type >> 8 & 0xff;
	riff->name[2] = type >> 16 & 0xff;
	riff->name[3] = type >> 24 & 0xff;
	riff->name[4] = '(';
	riff->name[5] = subtype & 0xff;
	riff->name[6] = subtype >> 8 & 0xff;
	riff->name[7] = subtype >> 16 & 0xff;
	riff->name[8] = subtype >> 24 & 0xff;
	riff->name[9] = ')';
	riff->name[10] = '\0';

	if ((rc = riff_write_header(riff))) {
		error("failed to write list chunk header");
		goto finally;
	}

	item->riff = riff;
	TAILQ_INSERT_TAIL(list, item, next);
	*out = riff;
	rc = 0;

finally:
	if (rc != 0) {
		if (riff) {
			free(riff);
		}
		if (item) {
			free(item);
		}
	}
	return rc;
}

int riff_list_add_leaf(struct riff_list *list,
		struct riff *parent, FILE *file,
		u_int32_t type, u_int32_t size, struct riff **out)
{
	int rc;
	struct riff *riff = NULL;
	struct riff_item *item = NULL;

	debug("adding leaf chunk");

	if (!(item = calloc(1, sizeof(*item)))) {
		rc = ENOMEM;
		error("failed to allocate riff_item");
		goto finally;
	}

	if (!(riff = calloc(1, sizeof(*riff)))) {
		rc = ENOMEM;
		error("failed to allocate riff");
		goto finally;
	}

	TAILQ_INIT(&riff->list);

	riff->parent = parent;
	riff->file = file;
	riff->type = FALCO_RIFF_LEAF;
	riff->offset = -1;
	riff->header.leaf.type = type;
	riff->header.leaf.size = size;
	riff->name[0] = type & 0xff;
	riff->name[1] = type >> 8 & 0xff;
	riff->name[2] = type >> 16 & 0xff;
	riff->name[3] = type >> 24 & 0xff;
	riff->name[4] = '\0';

	if ((rc = riff_write_header(riff))) {
		error("failed to write leaf chunk header");
		goto finally;
	}

	item->riff = riff;
	TAILQ_INSERT_TAIL(list, item, next);
	*out = riff;
	rc = 0;

finally:
	if (rc != 0) {
		if (rc != 0) {
			if (riff) {
				free(riff);
			}
			if (item) {
				free(item);
			}
		}
	}
	return rc;
}

int riff_list_write_header(struct riff_list *list)
{
	int rc;
	struct riff_item *item;

	TAILQ_FOREACH(item, list, next) {
		if ((rc = riff_write_header(item->riff))) {
			error("failed to update chunk: %s", strerror(rc));
			goto finally;
		}
	}

	rc = 0;

finally:
	return rc;
}

void riff_list_clear(struct riff_list *list)
{
	struct riff_item *item;

	debug("clearing head");

	while ((item = TAILQ_FIRST(list))) {
		TAILQ_REMOVE(list, item, next);
		riff_list_clear(&item->riff->list);
		free(item->riff);
		free(item);
	}
}

size_t riff_leaf_header_size(void)
{
	struct riff *riff;
	return sizeof(riff->header.leaf);
}

size_t riff_list_header_size(void)
{
	struct riff *riff;
	return sizeof(riff->header.list);
}

int riff_add_list(struct riff *riff,
		u_int32_t type, u_int32_t subtype, struct riff **child)
{
	int rc;

	if (riff->type != FALCO_RIFF_LIST) {
		rc = EINVAL;
		error("not a list chunk");
		goto finally;
	}

	if ((rc = riff_list_add_list(&riff->list, riff, riff->file,
			type, subtype, child))) {
		error("failed to add child list chunk: %s", strerror(rc));
		goto finally;
	}

finally:
	return rc;
}

int riff_add_leaf(struct riff *riff,
		u_int32_t type, u_int32_t size, struct riff **child)
{
	int rc;

	if (riff->type != FALCO_RIFF_LIST) {
		rc = EINVAL;
		error("not a list chunk");
		goto finally;
	}

	if ((rc = riff_list_add_leaf(&riff->list, riff, riff->file,
			type, size, child))) {
		error("failed to add child leaf chunk: %s", strerror(rc));
		goto finally;
	}

finally:
	return rc;
}

int riff_write(struct riff *riff, void *data, size_t len)
{
	int rc;

	debug("writing data of %s: %d bytes", riff->name, len);

	if (!fwrite(data, len, 1, riff->file)) {
		rc = errno ? errno : -1;
		error("failed to write data: %s", strerror(rc));
		goto finally;
	}

	if ((rc = riff_accumulate(riff, len))) {
		error("failed to accumulate size: %s",
				strerror(rc));
		goto finally;
	}

	rc = 0;

finally:
	return rc;
}

int riff_update(struct riff *riff, void *data, size_t len)
{
	int rc;
	off_t temp, offset;

	if (len > riff->size) {
		rc = EOVERFLOW;
		error("size exceeds: %d != %d", len, riff->size);
		goto finally;
	}

	switch (riff->type) {
	case FALCO_RIFF_LIST:
		offset = riff->offset + sizeof(riff->header.list);
		break;
	case FALCO_RIFF_LEAF:
		offset = riff->offset + sizeof(riff->header.leaf);
		break;
	default:
		rc = -1;
		error("unexpected type: %d", riff->type);
		goto finally;
	}

	if ((temp = ftell(riff->file)) == -1) {
		rc = errno ? errno : -1;
		error("failed to ftell(): %s", strerror(rc));
		goto finally;
	}

	if (fseek(riff->file, offset, 0) < 0) {
		rc = errno ? errno : -1;
		error("failed to seek to %ld: %s", offset,
				strerror(rc));
		goto finally;
	}

	debug("writing data of %s at %ld: %d bytes", riff->name,
			offset, len);

	if (!fwrite(data, len, 1, riff->file)) {
		rc = errno ? errno : -1;
		error("failed to write data: %s", strerror(rc));
		goto finally;
	}

	if (fseek(riff->file, temp, 0) < 0) {
		rc = errno ? errno : -1;
		error("failed to seek back to %ld: %s", temp,
				strerror(rc));
		goto finally;
	}

	rc = 0;

finally:
	return rc;
}

struct riff_list *riff_iterator(struct riff *riff)
{
	return &riff->list;
}

int riff_stat(struct riff *riff, struct riff_stat *stat)
{
	int rc;

	if (!riff || !stat) {
		rc = EINVAL;
		error("null argument(s)");
		goto finally;
	}

	stat->offset = riff->offset;
	stat->size = riff->size;
	rc = 0;

finally:
	return rc;
}

int riff_write_header(struct riff *riff)
{
	int rc;
	void *data;
	size_t len;
	u_int32_t *size;
	long temp;

	switch (riff->type) {
	case FALCO_RIFF_LIST:
		size = &riff->header.list.size;
		data = &riff->header.list;
		len = sizeof(riff->header.list);
		break;
	case FALCO_RIFF_LEAF:
		size = &riff->header.list.size;
		data = &riff->header.leaf;
		len = sizeof(riff->header.leaf);
		break;
	default:
		rc = -1;
		error("unexpected type: %d", riff->type);
		goto finally;
	}

	if (riff->offset < 0) {
		if ((riff->offset = ftell(riff->file)) == -1) {
			rc = errno ? errno : -1;
			error("failed to ftell(): %s", strerror(rc));
			goto finally;
		}

		debug("writing %s header for first time at: %ld",
				riff->name, riff->offset);

		if (!fwrite(data, sizeof(u_int8_t), len, riff->file)) {
			rc = errno ? errno : -1;
			error("failed to write header: %s", strerror(rc));
			goto finally;
		}

		if (riff->parent && (rc = riff_accumulate(
				riff->parent, len))) {
			error("failed to accumulate parent size: %s",
					strerror(rc));
			goto finally;
		}
	} else if (*size != riff->size) {
		debug("correcting %s size: %d => %d", riff->name,
				*size, riff->size);

		*size = riff->size;

		if ((temp = ftell(riff->file)) == -1) {
			rc = errno ? errno : -1;
			error("failed to ftell(): %s", strerror(rc));
			goto finally;
		}

		if (fseek(riff->file, riff->offset, 0) < 0) {
			rc = errno ? errno : -1;
			error("failed to seek to %ld: %s", riff->offset,
					strerror(rc));
			goto finally;
		}

		debug("refreshing %s header at: %ld",
				riff->name, riff->offset);

		if (!fwrite(data, sizeof(u_int8_t), len, riff->file)) {
			rc = errno ? errno : -1;
			error("failed to write header: %s", strerror(rc));
			goto finally;
		}

		if (fseek(riff->file, temp, 0) < 0) {
			rc = errno ? errno : -1;
			error("failed to seek back to %ld: %s", temp,
					strerror(rc));
			goto finally;
		}
	}

	if (riff->type == FALCO_RIFF_LIST) {
		if ((rc = riff_list_write_header(&riff->list))) {
			error("failed to write children headers: %s",
					strerror(rc));
			goto finally;
		}
	}

	rc = 0;

finally:
	return rc;
}

int riff_accumulate(struct riff *riff, size_t len)
{
	int rc;

	debug("accumulating %s by: %d+%d bytes", riff->name,
			riff->size, len);

	riff->size += len;

	if (riff->parent && (rc = riff_accumulate(
			riff->parent, len))) {
		error("failed to accumulate parent size: %s",
				strerror(rc));
		goto finally;
	}

	rc = 0;

finally:
	return rc;
}

