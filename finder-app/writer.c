/*
 * writer.c
 *
 * C replacement for the writer.sh test script from Assignment 1.
 *
 * Usage:
 *   writer <writefile> <writestr>
 *
 * Writes <writestr> to <writefile> using File I/O (open/write/close).
 * Assumes the directory containing <writefile> already exists; the
 * caller is responsible for creating it (unlike writer.sh, this
 * program will NOT create any directories).
 *
 * Logging:
 *   - Uses syslog with LOG_USER facility.
 *   - LOG_DEBUG: "Writing <writestr> to <writefile>"
 *   - LOG_ERR:   any unexpected error conditions
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <syslog.h>

int main(int argc, char *argv[])
{
    const char *writefile;
    const char *writestr;
    int fd;
    ssize_t bytes_written;
    size_t len;

    /* Open syslog with LOG_USER facility, tagging messages with the
     * program name and including the PID for traceability. */
    openlog("writer", LOG_PID | LOG_CONS, LOG_USER);

    if (argc != 3) {
        syslog(LOG_ERR,
               "Invalid number of arguments: %d (expected 2: <writefile> <writestr>)",
               argc - 1);
        fprintf(stderr,
                "Usage: %s <writefile> <writestr>\n", argv[0]);
        closelog();
        return 1;
    }

    writefile = argv[1];
    writestr = argv[2];

    /* Open (or create/truncate) the target file for writing.
     * We do NOT create any missing parent directories - the caller
     * is expected to have created them already. */
    fd = open(writefile, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd == -1) {
        syslog(LOG_ERR, "Could not open/create file '%s': %s",
               writefile, strerror(errno));
        fprintf(stderr, "Error: could not open '%s': %s\n",
                writefile, strerror(errno));
        closelog();
        return 1;
    }

    len = strlen(writestr);
    bytes_written = write(fd, writestr, len);
    if (bytes_written == -1) {
        syslog(LOG_ERR, "Error writing to file '%s': %s",
               writefile, strerror(errno));
        fprintf(stderr, "Error: could not write to '%s': %s\n",
                writefile, strerror(errno));
        close(fd);
        closelog();
        return 1;
    }

    if ((size_t)bytes_written != len) {
        syslog(LOG_ERR,
               "Short write to file '%s': wrote %zd of %zu bytes",
               writefile, bytes_written, len);
        fprintf(stderr,
                "Error: short write to '%s' (%zd of %zu bytes)\n",
                writefile, bytes_written, len);
        close(fd);
        closelog();
        return 1;
    }

    /* Required debug-level syslog message documenting the write. */
    syslog(LOG_DEBUG, "Writing %s to %s", writestr, writefile);

    if (close(fd) == -1) {
        syslog(LOG_ERR, "Error closing file '%s': %s",
               writefile, strerror(errno));
        fprintf(stderr, "Error: could not close '%s': %s\n",
                writefile, strerror(errno));
        closelog();
        return 1;
    }

    closelog();
    return 0;
}
