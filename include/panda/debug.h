/*!
 * @file debug.h
 * @brief Macros for better debug output.
 */
#pragma once
#if !defined(__cplusplus)
#include <stdio.h>
#else
#include <cstdio>
#endif

/**
 * @brief Macro that evaluates to the basename of the current file.
 */
#define __FILENAME__ (__builtin_strrchr(__FILE__, '/') ? __builtin_strrchr(__FILE__, '/') + 1 : __FILE__)

/**
 * @brief Name to report in messages when PLUGIN_NAME is not set.
 */
#define PANDA_CORE_NAME "core"

/**
 * @brief Prefix for the PANDA message prefix.
 */
#define PANDA_MSG_PREFIX "PANDA["

/**
 * @brief Suffix for the PANDA message prefix.
 */
#define PANDA_MSG_SUFFIX "]:"

/**
 * @brief Format for creating a PANDA message prefix with a dynamic plugin name.
 *
 * @note This comes handy in plugin initialization code, where you want to
 * report a plugin name rather than the less specific PANDA_CORE_NAME.
 */
#define PANDA_MSG_FMT PANDA_MSG_PREFIX "%s" PANDA_MSG_SUFFIX

/**
 * @brief PANDA message prefix macro.
 */
#ifdef PLUGIN_NAME
#define PANDA_MSG PANDA_MSG_PREFIX PLUGIN_NAME PANDA_MSG_SUFFIX
#else
#define PANDA_MSG PANDA_MSG_PREFIX PANDA_CORE_NAME PANDA_MSG_SUFFIX
#endif

/**
 * @brief Get a textual representation of a flag variable.
 */
#define PANDA_FLAG_STATUS(flag) ((flag) ? "ENABLED" : "DISABLED")

/**
 * @brief PANDA log-levels.
 */
#define PANDA_LOG_NOTHING 0
#define PANDA_LOG_ERROR   1
#define PANDA_LOG_WARNING 2
#define PANDA_LOG_INFO    3
#define PANDA_LOG_DEBUG   4

/**
 * @brief Set default PANDA log-level.
 */
#if !defined(PANDA_LOG_LEVEL)
#define PANDA_LOG_LEVEL 2
#endif

/**
 * @brief Macro for logging error messages.
 */
#if !defined(PANDA_ERROR_FILE)
#define LOG_ERROR_FILE stderr
#endif
#if PANDA_LOG_LEVEL < PANDA_LOG_ERROR
#define LOG_ERROR(fmt, args...) {}
#else
#define LOG_ERROR(fmt, args...) fprintf(LOG_ERROR_FILE, PANDA_MSG "E:%s(%s)> " fmt "\n", __FILENAME__, __func__, ## args)
#endif

/**
 * @brief Macro for logging warning messages.
 */
#if !defined(LOG_WARNING_FILE)
#define LOG_WARNING_FILE stderr
#endif
#if PANDA_LOG_LEVEL < PANDA_LOG_WARNING
#define LOG_WARNING(fmt, args...) {}
#else
#define LOG_WARNING(fmt, args...)  fprintf(LOG_WARNING_FILE, PANDA_MSG "W> "  fmt "\n", ## args)
#endif

/**
 * @brief Macro for logging informational messages.
 */
#if !defined(LOG_INFO_FILE)
#define LOG_INFO_FILE stderr
#endif
#if PANDA_LOG_LEVEL < PANDA_LOG_INFO
#define LOG_INFO(fmt, args...) {}
#else
#define LOG_INFO(fmt, args...)  fprintf(LOG_INFO_FILE, PANDA_MSG "I> "  fmt "\n", ## args)
#endif

/**
 * @brief Macro for logging debug messages.
 */
#if !defined(LOG_DEBUG_FILE)
#define LOG_DEBUG_FILE stderr
#endif
#if !defined(LOG_PANDALN_FILE)
#define LOG_PANDALN_FILE stdout
#endif

#if PANDA_LOG_LEVEL < PANDA_LOG_DEBUG
#define LOG_DEBUG(fmt, args...) {}
#define PANDALN
#else
#define LOG_DEBUG(fmt, args...)  fprintf(LOG_DEBUG_FILE, PANDA_MSG "D> "  fmt "\n", ## args)
#define PANDALN fprintf(LOG_PANDALN_FILE, "-> %s:%03d %s()\n", __FILENAME__, __LINE__, __func__)
#endif

/* vim:set tabstop=4 softtabstop=4 expandtab: */
