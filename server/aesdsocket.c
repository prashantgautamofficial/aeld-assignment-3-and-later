/*
 * aesdsocket.c
 *
 * AESD Assignment 5 Part 1 - stream socket server.
 *
 * Opens a stream socket on port 9000, accepts connections, receives
 * newline-delimited data packets, appends each completed packet to
 * /var/tmp/aesdsocketdata, and returns the full contents of that file
 * back to the client after each packet is received.
 *
 * Supports:
 *   - Logging via syslog (LOG_USER facility)
 *   - Graceful shutdown on SIGINT/SIGTERM (closes sockets, removes data file)
 *   - "-d" argument to run as a daemon (fork occurs only after successful bind)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <fcntl.h>
#include <syslog.h>
#include <stdbool.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

#define PORT            "9000"
#define DATA_FILE       "/var/tmp/aesdsocketdata"
#define BACKLOG         10
#define RECV_CHUNK_SIZE 1024

/* Globals needed by the signal handler for graceful cleanup */
static volatile sig_atomic_t g_shutdown_requested = 0;
static int g_listen_fd = -1;
static int g_client_fd = -1;

static void signal_handler(int signo)
{
    (void)signo;
    g_shutdown_requested = 1;

    /*
     * Shut down whichever sockets are currently open so any blocking
     * accept()/recv() call unblocks with an error we can check for.
     * Only async-signal-safe calls are used here.
     */
    if (g_client_fd != -1)
    {
        shutdown(g_client_fd, SHUT_RDWR);
    }
    if (g_listen_fd != -1)
    {
        shutdown(g_listen_fd, SHUT_RDWR);
    }
}

static int setup_signal_handlers(void)
{
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0; /* no SA_RESTART: we want blocking calls to return EINTR */

    if (sigaction(SIGINT, &sa, NULL) == -1)
    {
        syslog(LOG_ERR, "sigaction(SIGINT) failed: %s", strerror(errno));
        return -1;
    }
    if (sigaction(SIGTERM, &sa, NULL) == -1)
    {
        syslog(LOG_ERR, "sigaction(SIGTERM) failed: %s", strerror(errno));
        return -1;
    }
    return 0;
}

/*
 * Creates, binds, and starts listening on a stream socket for PORT.
 * Returns the listening fd, or -1 on any failure.
 */
static int create_and_bind_listen_socket(void)
{
    struct addrinfo hints, *res, *rp;
    int sockfd = -1;
    int yes = 1;
    int rv;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    rv = getaddrinfo(NULL, PORT, &hints, &res);
    if (rv != 0)
    {
        syslog(LOG_ERR, "getaddrinfo failed: %s", gai_strerror(rv));
        return -1;
    }

    for (rp = res; rp != NULL; rp = rp->ai_next)
    {
        sockfd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (sockfd == -1)
        {
            continue;
        }

        if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) == -1)
        {
            syslog(LOG_ERR, "setsockopt failed: %s", strerror(errno));
            close(sockfd);
            sockfd = -1;
            continue;
        }

        if (bind(sockfd, rp->ai_addr, rp->ai_addrlen) == -1)
        {
            close(sockfd);
            sockfd = -1;
            continue;
        }

        break; /* success */
    }

    freeaddrinfo(res);

    if (sockfd == -1)
    {
        syslog(LOG_ERR, "Failed to bind socket on port %s: %s", PORT, strerror(errno));
        return -1;
    }

    if (listen(sockfd, BACKLOG) == -1)
    {
        syslog(LOG_ERR, "listen failed: %s", strerror(errno));
        close(sockfd);
        return -1;
    }

    return sockfd;
}

/*
 * Receives a full connection's worth of newline-delimited packets from
 * client_fd, appending each completed packet to DATA_FILE and writing the
 * full file content back to the client immediately after each packet.
 *
 * Returns 0 on normal connection close, -1 on fatal error.
 */
static int handle_client(int client_fd)
{
    char *buf = NULL;
    size_t buf_cap = 0;
    size_t buf_len = 0;
    char chunk[RECV_CHUNK_SIZE];
    ssize_t nread;

    for (;;)
    {
        nread = recv(client_fd, chunk, sizeof(chunk), 0);

        if (nread == 0)
        {
            /* Client closed connection */
            break;
        }
        if (nread == -1)
        {
            if (errno == EINTR)
            {
                /* Interrupted by our signal handler during shutdown */
                free(buf);
                return -1;
            }
            syslog(LOG_ERR, "recv failed: %s", strerror(errno));
            free(buf);
            return -1;
        }

        /* Grow the accumulation buffer to fit the new chunk */
        {
            size_t needed = buf_len + (size_t)nread;
            if (needed > buf_cap)
            {
                size_t new_cap = buf_cap == 0 ? RECV_CHUNK_SIZE : buf_cap;
                while (new_cap < needed)
                {
                    new_cap *= 2;
                }
                char *tmp = realloc(buf, new_cap);
                if (tmp == NULL)
                {
                    syslog(LOG_ERR, "malloc/realloc failed while buffering packet, discarding");
                    /* Discard what we have so far and reset, per assignment allowance */
                    free(buf);
                    buf = NULL;
                    buf_cap = 0;
                    buf_len = 0;
                    continue;
                }
                buf = tmp;
                buf_cap = new_cap;
            }
        }

        memcpy(buf + buf_len, chunk, (size_t)nread);
        buf_len += (size_t)nread;

        /* Process as many complete (newline-terminated) packets as we have */
        for (;;)
        {
            char *nl = memchr(buf, '\n', buf_len);
            if (nl == NULL)
            {
                break;
            }

            size_t packet_len = (size_t)(nl - buf) + 1; /* include the newline */

            /* Append this packet to the data file */
            int fd = open(DATA_FILE, O_WRONLY | O_CREAT | O_APPEND, 0644);
            if (fd == -1)
            {
                syslog(LOG_ERR, "open(%s) failed: %s", DATA_FILE, strerror(errno));
                free(buf);
                return -1;
            }

            size_t written = 0;
            while (written < packet_len)
            {
                ssize_t w = write(fd, buf + written, packet_len - written);
                if (w == -1)
                {
                    if (errno == EINTR)
                    {
                        continue;
                    }
                    syslog(LOG_ERR, "write(%s) failed: %s", DATA_FILE, strerror(errno));
                    close(fd);
                    free(buf);
                    return -1;
                }
                written += (size_t)w;
            }
            close(fd);

            /* Send the full file content back to the client */
            fd = open(DATA_FILE, O_RDONLY);
            if (fd == -1)
            {
                syslog(LOG_ERR, "open(%s) for read failed: %s", DATA_FILE, strerror(errno));
                free(buf);
                return -1;
            }

            char sendbuf[RECV_CHUNK_SIZE];
            ssize_t r;
            while ((r = read(fd, sendbuf, sizeof(sendbuf))) > 0)
            {
                ssize_t sent_total = 0;
                while (sent_total < r)
                {
                    ssize_t s = send(client_fd, sendbuf + sent_total, (size_t)(r - sent_total), 0);
                    if (s == -1)
                    {
                        if (errno == EINTR)
                        {
                            continue;
                        }
                        syslog(LOG_ERR, "send failed: %s", strerror(errno));
                        close(fd);
                        free(buf);
                        return -1;
                    }
                    sent_total += s;
                }
            }
            if (r == -1)
            {
                syslog(LOG_ERR, "read(%s) failed: %s", DATA_FILE, strerror(errno));
            }
            close(fd);

            /* Remove the processed packet from the front of buf */
            size_t remaining = buf_len - packet_len;
            memmove(buf, buf + packet_len, remaining);
            buf_len = remaining;
        }
    }

    free(buf);
    return 0;
}

static int daemonize(void)
{
    pid_t pid = fork();

    if (pid == -1)
    {
        syslog(LOG_ERR, "fork failed while daemonizing: %s", strerror(errno));
        return -1;
    }
    if (pid > 0)
    {
        /* Parent exits, letting the child continue as the daemon */
        exit(EXIT_SUCCESS);
    }

    /* Child continues here */
    if (setsid() == -1)
    {
        syslog(LOG_ERR, "setsid failed: %s", strerror(errno));
        return -1;
    }

    if (chdir("/") == -1)
    {
        syslog(LOG_ERR, "chdir(/) failed: %s", strerror(errno));
        return -1;
    }

    /* Redirect standard fds to /dev/null */
    int devnull = open("/dev/null", O_RDWR);
    if (devnull != -1)
    {
        dup2(devnull, STDIN_FILENO);
        dup2(devnull, STDOUT_FILENO);
        dup2(devnull, STDERR_FILENO);
        if (devnull > STDERR_FILENO)
        {
            close(devnull);
        }
    }

    return 0;
}

int main(int argc, char *argv[])
{
    bool run_as_daemon = false;

    if (argc > 1 && strcmp(argv[1], "-d") == 0)
    {
        run_as_daemon = true;
    }

    openlog("aesdsocket", LOG_PID, LOG_USER);

    if (setup_signal_handlers() == -1)
    {
        closelog();
        return -1;
    }

    g_listen_fd = create_and_bind_listen_socket();
    if (g_listen_fd == -1)
    {
        closelog();
        return -1;
    }

    if (run_as_daemon)
    {
        /* Fork happens only after a successful bind, per assignment requirement */
        if (daemonize() == -1)
        {
            close(g_listen_fd);
            closelog();
            return -1;
        }
    }

    while (!g_shutdown_requested)
    {
        struct sockaddr_storage client_addr;
        socklen_t addr_len = sizeof(client_addr);

        int client_fd = accept(g_listen_fd, (struct sockaddr *)&client_addr, &addr_len);
        if (client_fd == -1)
        {
            if (errno == EINTR || g_shutdown_requested)
            {
                break;
            }
            syslog(LOG_ERR, "accept failed: %s", strerror(errno));
            continue;
        }

        g_client_fd = client_fd;

        char ip_str[INET6_ADDRSTRLEN] = {0};
        if (client_addr.ss_family == AF_INET)
        {
            struct sockaddr_in *s = (struct sockaddr_in *)&client_addr;
            inet_ntop(AF_INET, &s->sin_addr, ip_str, sizeof(ip_str));
        }
        else if (client_addr.ss_family == AF_INET6)
        {
            struct sockaddr_in6 *s = (struct sockaddr_in6 *)&client_addr;
            inet_ntop(AF_INET6, &s->sin6_addr, ip_str, sizeof(ip_str));
        }
        else
        {
            strncpy(ip_str, "unknown", sizeof(ip_str) - 1);
        }

        syslog(LOG_INFO, "Accepted connection from %s", ip_str);

        handle_client(client_fd);

        syslog(LOG_INFO, "Closed connection from %s", ip_str);

        close(client_fd);
        g_client_fd = -1;
    }

    syslog(LOG_INFO, "Caught signal, exiting");

    if (g_listen_fd != -1)
    {
        close(g_listen_fd);
        g_listen_fd = -1;
    }

    if (remove(DATA_FILE) == -1 && errno != ENOENT)
    {
        syslog(LOG_ERR, "Failed to remove %s: %s", DATA_FILE, strerror(errno));
    }

    closelog();
    return 0;
}
