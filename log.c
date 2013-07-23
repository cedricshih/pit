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

#include "log.h"

static enum pit_log_level log_level = PIT_WARN;

void pit_set_log_level(enum pit_log_level level)
{
	log_level = level;
}

static pit_log_cb log_cb = NULL;
static void *log_cbarg = NULL;

void pit_set_log_cb(pit_log_cb cb, void *cbarg)
{
	log_cb = cb;
	log_cbarg = cbarg;
}

void pit_log(enum pit_log_level level,
                const char *func, int line,
                const char *fmt, ...)
{
        va_list ap;

        va_start(ap, fmt);
        pit_vlog(level, func, line, fmt, ap);
        va_end(ap);
}

static const char *last_func = NULL;
static unsigned int last_line = 0;
static int suppressing = 0;

void pit_vlog(enum pit_log_level level,
                const char *func, int line,
                const char *fmt, va_list ap)
{
        const char *prio;
        FILE *file = stdout;

	if (log_cb) {
		(*log_cb)(level, func, line, fmt, ap, log_cbarg);
	} else {
	        switch (level) {
	        case PIT_TRACE:
	                prio = "V";
	                break;
	        case PIT_DEBUG:
	                prio = "D";
	                break;
	        case PIT_INFO:
	                prio = "I";
	                break;
	        case PIT_WARN:
	                prio = "W";
	                break;
	        case PIT_ERROR:
	                prio = "E";
	                break;
	        case PIT_FATAL:
	        default:
	                prio = "F";
	                break;
	        }

	        if (level < log_level) {
	        	return;
	        }

	        if (level >= PIT_WARN) {
	                file = stderr;
	        }

//	        if (func == last_func && line == last_line) {
//	                if (suppressing) {
//	                        return;
//	                }
//
//	                fprintf(file, "(suppressing...)\n");
//
//	                suppressing = 1;
//	        } else {
	                fprintf(file, "%s %s (%d) - ", prio, func, line);
	                vfprintf(file, fmt, ap);
	                fprintf(file, "\n");

	                fflush(file);

	                suppressing = 0;
	                last_func = func;
	                last_line = line;
//	        }
	}
}




