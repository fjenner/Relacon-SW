/*
Copyright 2021 Frank Jenner

Redistribution and use in source and binary forms, with or without modification,
are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this
list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright notice,
this list of conditions and the following disclaimer in the documentation and/or
other materials provided with the distribution.

3. Neither the name of the copyright holder nor the names of its contributors
may be used to endorse or promote products derived from this software without
specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR
ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#ifndef LOG_H
#define LOG_H

/**
 * A log message severity
 */
enum LogLevel
{
    LOG_LEVEL_ERROR,
    LOG_LEVEL_WARNING,
    LOG_LEVEL_INFO,
    LOG_LEVEL_DEBUG
};

/**
 * Represents an instance of a log. Currently there is only a single log
 * implementation that prints to stderr, though this structure could potentially
 * store a pointer to a logging backend in the future.
 */
struct Log
{
    /** The log level at which to print log messages */
    enum LogLevel threshold;
};

// Convenience functions for logging at specific log levels
#define LOG_ERROR(log, fmt, ...) LOG_PRINT(log, LOG_LEVEL_ERROR, fmt, ##__VA_ARGS__)
#define LOG_WARNING(log, fmt, ...) LOG_PRINT(log, LOG_LEVEL_WARNING, fmt, ##__VA_ARGS__)
#define LOG_INFO(log, fmt, ...) LOG_PRINT(log, LOG_LEVEL_INFO, fmt, ##__VA_ARGS__)
#define LOG_DEBUG(log, fmt, ...) LOG_PRINT(log, LOG_LEVEL_DEBUG, fmt, ##__VA_ARGS__)

// Include file and line information if compiled with DEBUG defined. Typically
// not wanted for release builds due to non-reproducibility.
#ifdef DEBUG
#define LOG_PRINT(log, level, fmt, ...) LogPrint(log, level, "%s:%u: " fmt, __FILE__, __LINE__, ##__VA_ARGS__)
#else
#define LOG_PRINT(log, level, fmt, ...) LogPrint(log, level, fmt, ##__VA_ARGS__)
#endif

/**
 * Initialize a log instance
 *
 * @param[out] log The log instance to initialize
 */
void LogInit(struct Log *log);

/**
 * Prints the message to the specified log if the log level is sufficient
 *
 * @param[in] log The log instance to print to. Currently only used for storing
 *                the current log level. Can be NULL to use a "default" log.
 * @param[in] level The log level of the message to log
 * @param[in] fmt A printf-compatible format string
 */
void LogPrint(struct Log *log, enum LogLevel level, const char *fmt, ...);

#endif