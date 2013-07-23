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
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "log.h"

#include "filelist.h"

int filelist_list(struct filelist *list, const char *path, size_t *total,
		filelist_filter_cb filter_cb, void *cbarg)
{
	int rc;
	DIR *dir = NULL;
	struct dirent *ent;
	char buffer[PATH_MAX], *c;
	size_t count = 0;

	if (!(dir = opendir(path))) {
		rc = errno ? errno : -1;
		error("opendir: %s", strerror(rc));
		goto finally;
	}

	/* print all the files and directories within directory */
	while ((ent = readdir(dir)) != NULL ) {
		if (!strcmp(ent->d_name, ".") || !strcmp(ent->d_name, "..")) {
			continue;
		}

		if (filter_cb) {
			c = strrchr(ent->d_name, '.');

			if (!((*filter_cb)(ent->d_name, c ? c + 1 : c, cbarg))) {
				continue;
			}
		}

		snprintf(buffer, sizeof(buffer), "%s/%s", path, ent->d_name);

		if ((rc = filelist_add(list, buffer))) {
			if (rc == EISDIR) {
				continue;
			}

			error("filelist_add: %s\n", strerror(rc));
			goto finally;
		}

		count++;
	}

	if (total) {
		*total = count;
	}

	rc = 0;

finally:
	if (dir) {
		closedir(dir);
	}
	return rc;
}

int filelist_add(struct filelist *list, const char *path)
{
	int rc;
	struct file *item = NULL;
	struct stat st;

	if ((rc = stat(path, &st))) {
		rc = errno ? errno : rc;
		goto finally;
	}

	if (S_ISDIR(st.st_mode)) {
		rc = EISDIR;
		goto finally;
	}

	if (!(item = calloc(1, sizeof(*item)))) {
		rc = errno ? errno : -1;
		goto finally;
	}

	if (!(item->path = strdup(path))) {
		rc = errno ? errno : -1;
		goto finally;
	}

	if (RB_FIND(filelist, list, item)) {
		rc = EEXIST;
		goto finally;
	}

	RB_INSERT(filelist, list, item);
	rc = 0;

finally:
	if (rc) {
		if (item) {
			if (item->path) {
				free(item->path);
			}
			free(item);
		}
	}
	return rc;
}

void filelist_clear(struct filelist *list)
{
	struct file *item;

	while ((item = RB_MIN(filelist, list))) {
		RB_REMOVE(filelist, list, item);

		if (item->path) {
			free(item->path);
		}

		free(item);
	}
}

int file_cmp(struct file *a, struct file *b)
{
	return strcmp(a->path, b->path);
}

RB_GENERATE(filelist, file, entry, file_cmp);
