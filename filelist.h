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


#ifndef FILELIST_H_
#define FILELIST_H_

#include "sys/tree.h"

struct file {
	char *path;
	RB_ENTRY(file) entry;
};

RB_HEAD(filelist, file);

typedef int (*filelist_filter_cb)(const char *filename, const char *extname,
		void *cbarg);

int filelist_list(struct filelist *list, const char *dir, size_t *total,
		filelist_filter_cb filter_cb, void *cbarg);

int filelist_add(struct filelist *list, const char *path);

void filelist_clear(struct filelist *list);

int file_cmp(struct file *a, struct file *b);

RB_PROTOTYPE(filelist, file, entry, file_cmp);

#endif /* FILELIST_H_ */
