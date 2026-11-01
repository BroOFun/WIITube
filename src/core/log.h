/* Tiny logger: desktop logs to stderr; the Wii logs to a file on the SD card so problems
 * can be reported back. */
#ifndef WIITUBE_LOG_H
#define WIITUBE_LOG_H

/* Open a log file (appending). NULL or failure keeps logging to stderr. */
void wlog_open(const char *path);
void wlog_close(void);
void wlog(const char *fmt, ...)
#ifdef __GNUC__
    __attribute__((format(printf, 1, 2)))
#endif
    ;

#endif
