/*
 * util.h - useful functions header
 *
 * Copyright © 2007-2008 Julien Danjou <julien@danjou.info>
 * Copyright © 2006 Pierre Habouzit <madcoder@debian.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 */
#pragma once

#include <fmt/core.h>
#include <source_location>
#include <type_traits>
#include <utility>
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <assert.h>
#include <cstdint>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/types.h>

#if !(defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__) || defined(__DragonFly__))
#include <alloca.h>
#endif

#include <cctype>

#include <fmt/format.h>

/** \brief replace \c NULL strings with empty strings */
#define NONULL(x) (x ? x : "")

#define DO_NOTHING(...)

#undef MAX
#undef MIN
#define MAX(a, b) ((a) < (b) ? (b) : (a))
#define MIN(a, b) ((a) < (b) ? (a) : (b))

namespace range {
template <template <typename...> class Container>
struct to {
    template <typename Range>
    friend auto operator|(Range&& range, to&& to) {
        return Container(range.begin(), range.end());
    }
};
}

#define unsigned_subtract(a, b) \
    do {                        \
        if (b > a)              \
            a = 0;              \
        else                    \
            a -= b;             \
    } while (0)


#define p_alloca(type, count) \
    ((type*)memset(alloca(sizeof(type) * (count)), 0, sizeof(type) * (count)))

#define p_clear(p, count) ((void)memset((p), 0, sizeof(*(p)) * (count)))
#define p_dup(p, count) xmemdup((p), sizeof(*(p)) * (count))

template <typename PtrT>
requires(!std::is_const_v<PtrT>)
void p_delete(PtrT** ptr) {
    free(*ptr);
    *ptr = nullptr;
}

template <typename PtrT>
requires(std::is_const_v<PtrT>)
void p_delete(PtrT** ptr) {
    free(*const_cast<std::remove_const_t<PtrT>**>(ptr));
    *ptr = nullptr;
}

#ifdef __GNUC__
#define likely(expr) __builtin_expect(!!(expr), 1)
#define unlikely(expr) __builtin_expect((expr), 0)
#else
#define likely(expr) expr
#define unlikely(expr) expr
#endif

static inline void* __attribute__((malloc)) xmalloc(ssize_t size) {
    void* ptr;

    if (size <= 0) {
        return NULL;
    }

    ptr = calloc(1, size);

    if (!ptr) {
        abort();
    }

    return ptr;
}

/** Duplicate a memory zone.
 * \param src The source.
 * \param size The source size.
 * \return The memory address of the copy.
 */
static inline void* xmemdup(const void* src, ssize_t size) {
    return memcpy(xmalloc(size), src, size);
}

/** \brief \c NULL resistant strlen.
 *
 * Unlike it's libc sibling, a_strlen returns a ssize_t, and supports its
 * argument being NULL.
 *
 * \param[in] s the string.
 * \return the string length (or 0 if \c s is \c NULL).
 */
static inline ssize_t a_strlen(const char* s) { return s ? strlen(s) : 0; }

/** \brief \c NULL resistant strnlen.
 *
 * Unlike it's GNU libc sibling, a_strnlen returns a ssize_t, and supports
 * its argument being NULL.
 *
 * The a_strnlen() function returns the number of characters in the string
 * pointed to by \c s, not including the terminating \c \\0 character, but at
 * most \c n. In doing this, a_strnlen() looks only at the first \c n
 * characters at \c s and never beyond \c s+n.
 *
 * \param[in]  s    the string.
 * \param[in]  n    the maximum length to return.
 * \return \c a_strlen(s) if less than \c n, else \c n.
 */
static inline ssize_t a_strnlen(const char* s, ssize_t n) {
    if (s) {
        const char* p = (const char*)memchr(s, '\0', n);
        return p ? p - s : n;
    }
    return 0;
}

/** \brief safe limited strdup.
 *
 * Copies at most min(<tt>n-1</tt>, \c l) characters from \c src into a newly
 * allocated buffer, always adding a final \c \\0, and returns that buffer.
 *
 * \warning when s is \c "" or l is 0, it returns NULL !
 *
 * \param[in]  s        source string.
 * \param[in]  l        maximum number of chars to copy.
 * \return a newly allocated buffer containing the first \c l chars of \c src.
 */
static inline char* a_strndup(const char* s, ssize_t l) {
    ssize_t len = MIN(a_strlen(s), l);
    if (len) {
        char* p = (char*)p_dup(s, len + 1);
        p[len] = '\0';
        return p;
    }
    return NULL;
}

/** \brief \c NULL resistant strcmp.
 * \param[in]  a     the first string.
 * \param[in]  b     the second string.
 * \return <tt>strcmp(a, b)</tt>, and treats \c NULL strings like \c ""
 * ones.
 */
static inline int a_strcmp(const char* a, const char* b) { return strcmp(NONULL(a), NONULL(b)); }

#define A_STREQ(a, b) (a_strcmp(a, b) == 0)
#define A_STRNEQ(a, b) (!A_STREQ(a, b))

/** \brief \c NULL resistant strcasecmp.
 * \param[in]  a     the first string.
 * \param[in]  b     the second string.
 * \return <tt>strcasecmp(a, b)</tt>, and treats \c NULL strings like \c ""
 * ones.
 */
static inline int a_strcasecmp(const char* a, const char* b) {
    return strcasecmp(NONULL(a), NONULL(b));
}

#define A_STREQ_CASE(a, b) (((a) == (b)) || a_strcasecmp(a, b) == 0)
#define A_STRNEQ_CASE(a, b) (!A_STREQ_CASE(a, b))


void log_messagev(char tag, FILE* file, const std::source_location loc, std::string_view format, fmt::format_args args);

template <typename ... Args>
void log_message(char tag, FILE *file, const std::source_location loc, std::string_view format, Args && ... args) {
    log_messagev(tag, file, loc, format, fmt::make_format_args(std::forward<Args>(args)...));
}

#define log_fatal(string, ...) log_message('E', stderr, std::source_location::current(), string, ## __VA_ARGS__)
#define log_warn(string, ...) log_message('W', stderr, std::source_location::current(), string, ## __VA_ARGS__)

#define awsm_check(condition)                                                        \
    do {                                                                             \
        if (!(condition))                                                            \
            log_warn("Checking assertion failed: " #condition); \
    } while (0)

const char* a_current_time_str(void);

void a_exec(const char*);

inline bool ichar_equals(char a, char b)
{
    return std::tolower(static_cast<unsigned char>(a)) ==
           std::tolower(static_cast<unsigned char>(b));
}
