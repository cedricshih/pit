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


#ifndef LOG_H_
#define LOG_H_

#include <stdarg.h>

enum pit_log_level {
        PIT_TRACE, /**< Trivial step tracing log. */
        PIT_DEBUG, /**< Debugging log. */
        PIT_INFO, /**< Informative log. */
        PIT_WARN, /**< Warning log. */
        PIT_ERROR, /**< Error log. */
        PIT_FATAL, /**< Fatal error log. */
};

void pit_set_log_level(enum pit_log_level level);

typedef void (*pit_log_cb)(enum pit_log_level level,
		const char *func, int line,
		const char *fmt, va_list ap, void *cbarg);

void pit_set_log_cb(pit_log_cb cb, void *cbarg);

#include <stdarg.h>

#define trace(fmt...) pit_log(PIT_TRACE, __func__, __LINE__, fmt)
#define debug(fmt...) pit_log(PIT_DEBUG, __func__, __LINE__, fmt)
#define info(fmt...) pit_log(PIT_INFO, __func__, __LINE__, fmt)
#define warn(fmt...) pit_log(PIT_WARN, __func__, __LINE__, fmt)
#define error(fmt...) pit_log(PIT_ERROR, __func__, __LINE__, fmt)
#define fatal(fmt...) pit_log(PIT_FATAL, __func__, __LINE__, fmt)

#ifdef __GNUC__
#define PIT_CHECK_FMT(a,b) __attribute__((format(printf, a, b)))
#else
#define PIT_CHECK_FMT(a,b)
#endif

void pit_log(enum pit_log_level level,
                const char *func, int line,
                const char *fmt, ...) PIT_CHECK_FMT(4,5);

void pit_vlog(enum pit_log_level level,
                const char *func, int line,
                const char *fmt, va_list ap);

#endif /* LOG_H_ */
