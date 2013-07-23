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

#ifndef RIFF_H_
#define RIFF_H_

#include <sys/types.h>
#include <sys/queue.h>
#include <stdio.h>

struct riff;

struct riff_item {
	struct riff *riff;
	TAILQ_ENTRY(riff_item) next;
};

TAILQ_HEAD(riff_list, riff_item);

size_t riff_leaf_header_size(void);

size_t riff_list_header_size(void);

struct riff_tree;

struct riff_tree *riff_tree_new(FILE *file);

void riff_tree_free(struct riff_tree *tree);

int riff_tree_add_list(struct riff_tree *tree,
		u_int32_t type, u_int32_t subtype, struct riff **child);

int riff_tree_add_leaf(struct riff_tree *tree,
		u_int32_t type, u_int32_t size, struct riff **child);

int riff_tree_refresh(struct riff_tree *tree);

int riff_add_list(struct riff *riff,
		u_int32_t type, u_int32_t subtype, struct riff **child);

int riff_add_leaf(struct riff *riff,
		u_int32_t type, u_int32_t size, struct riff **child);

int riff_add_fixed(struct riff *riff,
		u_int32_t type, struct riff **child,
		void *data, size_t len);

int riff_write(struct riff *riff, void *data, size_t len);

int riff_update(struct riff *riff, void *data, size_t len);

struct riff_list *riff_iterator(struct riff *riff);

struct riff_stat {
	size_t offset;
	size_t size;
};

int riff_stat(struct riff *riff, struct riff_stat *stat);


enum riff_type {
	FALCO_RIFF_LIST,
	FALCO_RIFF_LEAF,
};

struct riff {
	char name[11];
	FILE *file;
	enum riff_type type;
	off_t offset;
	size_t size;
	union {
		struct {
			u_int32_t type;
			u_int32_t size;
		} leaf;
		struct {
			u_int32_t type;
			u_int32_t size;
			u_int32_t subtype;
		} list;
	} header;
	struct riff *parent;
	struct riff_list list;
};

struct riff_tree {
	FILE *file;
	struct riff_list list;
};

int riff_list_add_list(struct riff_list *list,
		struct riff *parent, FILE *file,
		u_int32_t type, u_int32_t subtype, struct riff **riff);

int riff_list_add_leaf(struct riff_list *list,
		struct riff *parent, FILE *file,
		u_int32_t type, u_int32_t size, struct riff **out);

int riff_list_write_header(struct riff_list *list);

void riff_list_clear(struct riff_list *list);

int riff_write_header(struct riff *riff);

int riff_accumulate(struct riff *riff, size_t len);

#endif /* RIFF_H_ */
