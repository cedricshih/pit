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

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <libgen.h>

#include "timelapse.h"
#include "startrail.h"
#include "stretch.h"

typedef int (*pit_handler)(char *basename, int argc, char **argv);
typedef void (*pit_helper)(FILE *file, char *basename, char *cmd);

static int help_handler(char *basename, int argc, char **argv);
static void help_helper(FILE *file, char *basename, char *cmd);

static struct {
	const char *cmd;
	const char *desc;
	pit_handler handler;
	pit_helper helper;
} handlers[] = {
		{ "help", "show usage of command", help_handler, help_helper },
		{ "stretch", "strech contrast", stretch, stretch_help },
		{ "time", "create timelapse video", timelapse, timelapse_help },
		{ "star", "create star trail photograph", startrail, startrail_help },
};
static int num_handlers = sizeof(handlers) / sizeof(handlers[0]);

#define MAX(a,b) a > b ? a : b;

static void help(FILE *file, char *basename)
{
	int i, max_cmd_len, max_desc_len;
	char fmt[256];

	fprintf(file, "Usage: %s <command> ...\n\n"
			"Commands:\n",
			basename);

	max_cmd_len = max_desc_len = 0;

	for (i = 0; i < num_handlers; i++) {
		max_cmd_len = MAX(max_cmd_len, strlen(handlers[i].cmd));
		max_desc_len = MAX(max_desc_len, strlen(handlers[i].desc));
	}

	for (i = 0; i < num_handlers; i++) {
		snprintf(fmt, sizeof(fmt), "    %%s%%%ds - %%s\n",
				max_cmd_len - strlen(handlers[i].cmd));
		fprintf(file, fmt, handlers[i].cmd, "", handlers[i].desc);
	}

	fprintf(file, "\n");
}

void help_helper(FILE *file, char *basename, char *cmd)
{
	fprintf(file, "Usage: %s %s <command>\n\n", basename, cmd);
}

int help_handler(char *basename, int argc, char **argv)
{
	int rc, i;
	char *name;
	pit_helper helper = NULL;

	if (argc < 2) {
		rc = EINVAL;
		help_helper(stderr, basename, argv[0]);
		goto finally;
	}

	name = argv[1];

	for (i = 0; i < num_handlers; i++) {
		if (!strcmp(name, handlers[i].cmd)) {
			helper = handlers[i].helper;
		}
	}

	if (!helper) {
		rc = EINVAL;
		fprintf(stderr, "Unknown command: %s\n\n", name);
		help(stderr, basename);
		goto finally;
	}

	(*helper)(stdout, basename, name);
	rc = 0;

finally:
	return rc;
}

int main(int argc, char **argv)
{
	int rc, i;
	char *name;
	pit_handler handler = NULL;

	name = basename(*argv++);
	argc--;

	if (argc < 1) {
		rc = EINVAL;
		help(stderr, name);
		goto finally;
	}

	for (i = 0; i < num_handlers; i++) {
		if (!strcmp(argv[0], handlers[i].cmd)) {
			handler = handlers[i].handler;
		}
	}

	if (!handler) {
		rc = EINVAL;
		fprintf(stderr, "Unknown command: %s\n\n", argv[0]);
		help(stderr, name);
		goto finally;
	}

	rc = (*handler)(name, argc, argv);

finally:
	return rc;
}
