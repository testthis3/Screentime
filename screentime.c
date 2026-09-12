#define _POSIX_C_SOURCE 200809L

#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <sys/stat.h>
#include <limits.h>
#include <signal.h>
#include <errno.h>

#define POLL_INTERVAL 1
#define MIN_SESSION_SECONDS 60

static volatile sig_atomic_t running = 1;

typedef struct {
    char app[256];
    char title[1024];
} WindowInfo;

static int x11_error_handler(Display *display, XErrorEvent *error)
{
    (void)display;
    (void)error;
    return 0;
}

static void handle_signal(int sig)
{
    (void)sig;
    running = 0;
}

static void sanitize_string(char *s)
{
    for (char *p = s; *p; p++) {
        if (*p == '\n' || *p == '\r')
            *p = ' ';
    }

static int get_window_property_string(
    Display *display,
    Window window,
    Atom property,
    char *buffer,
    size_t buffer_size
) {
    Atom actual_type;
    int actual_format;
    unsigned long nitems;
    unsigned long bytes_after;
    unsigned char *data = NULL;

    buffer[0] = '\0';

    int status = XGetWindowProperty(
        display,
        window,
        property,
        0,
        1024,
        False,
        AnyPropertyType,
        &actual_type,
        &actual_format,
        &nitems,
        &bytes_after,
        &data
    );

    if (status != Success || !data)
        return 0;

    size_t copy_size = nitems;

    if (copy_size >= buffer_size)
        copy_size = buffer_size - 1;

    memcpy(buffer, data, copy_size);
    buffer[copy_size] = '\0';

    XFree(data);

    sanitize_string(buffer);

    return 1;
}

static int get_window_title(
    Display *display,
    Window window,
    char *buffer,
    size_t buffer_size
) {
    Atom utf8_string = XInternAtom(display, "UTF8_STRING", False);
    Atom net_wm_name = XInternAtom(display, "_NET_WM_NAME", False);

    Atom actual_type;
    int actual_format;
    unsigned long nitems;
    unsigned long bytes_after;
    unsigned char *data = NULL;

    int status = XGetWindowProperty(
        display,
        window,
        net_wm_name,
        0,
        4096,
        False,
        utf8_string,
        &actual_type,
        &actual_format,
        &nitems,
        &bytes_after,
        &data
    );

    if (status == Success && data && nitems > 0) {
        size_t copy_size = nitems;

        if (copy_size >= buffer_size)
            copy_size = buffer_size - 1;

        memcpy(buffer, data, copy_size);
        buffer[copy_size] = '\0';

        XFree(data);

        sanitize_string(buffer);
        return 1;
    }

    if (data)
        XFree(data);

    /*
     * Older applications may only provide WM_NAME.
     */
    Atom wm_name = XInternAtom(display, "WM_NAME", False);

    return get_window_property_string(
        display,
        window,
        wm_name,
        buffer,
        buffer_size
    );
}

static int get_window_info(
    Display *display,
    Window window,
    WindowInfo *info
) {
    memset(info, 0, sizeof(*info));

    /*
     * WM_CLASS gives two strings:
     *
     * instance
     * class
     *
     * We use the class as the application name.
     */
    XClassHint class_hint;

    if (XGetClassHint(display, window, &class_hint)) {

        if (class_hint.res_class) {
            snprintf(
                info->app,
                sizeof(info->app),
                "%s",
                class_hint.res_class
            );
        } else if (class_hint.res_name) {
            snprintf(
                info->app,
                sizeof(info->app),
                "%s",
                class_hint.res_name
            );
        }

        if (class_hint.res_name)
            XFree(class_hint.res_name);

        if (class_hint.res_class)
            XFree(class_hint.res_class);
    }

    get_window_title(
        display,
        window,
        info->title,
        sizeof(info->title)
    );

    if (info->app[0] == '\0')
        snprintf(info->app, sizeof(info->app), "unknown");

    if (info->title[0] == '\0')
        snprintf(info->title, sizeof(info->title), "untitled");

    return 1;
}

static Window get_active_window(Display *display)
{
    Window root = DefaultRootWindow(display);

    Atom active_atom =
        XInternAtom(display, "_NET_ACTIVE_WINDOW", False);

    Atom actual_type;
    int actual_format;

    unsigned long nitems;
    unsigned long bytes_after;

    unsigned char *data = NULL;

    int status = XGetWindowProperty(
        display,
        root,
        active_atom,
        0,
        1,
        False,
        XA_WINDOW,
        &actual_type,
        &actual_format,
        &nitems,
        &bytes_after,
        &data
    );

    if (status != Success || !data || nitems == 0) {
        if (data)
            XFree(data);

        return None;
    }

    Window window = *((Window *)data);

    XFree(data);

    return window;
}

static void make_log_directory(void)
{
    char path[PATH_MAX];

    const char *home = getenv("HOME");

    if (!home) {
        fprintf(stderr, "HOME is not set\n");
        exit(EXIT_FAILURE);
    }

    snprintf(
        path,
        sizeof(path),
        "%s/.local/share",
        home
    );

    mkdir(path, 0755);

    snprintf(
        path,
        sizeof(path),
        "%s/.local/share/screentime",
        home
    );

    mkdir(path, 0755);
}

static FILE *open_daily_log(struct tm *local_time)
{
    const char *home = getenv("HOME");

    if (!home)
        return NULL;

    char path[PATH_MAX];

    snprintf(
        path,
        sizeof(path),
        "%s/.local/share/screentime/%04d-%02d-%02d.log",
        home,
        local_time->tm_year + 1900,
        local_time->tm_mon + 1,
        local_time->tm_mday
    );

    FILE *file = fopen(path, "a");

    if (!file) {
        fprintf(
            stderr,
            "Could not open %s: %s\n",
            path,
            strerror(errno)
        );

        return NULL;
    }

    return file;
}

static void format_duration(
    time_t seconds,
    char *buffer,
    size_t buffer_size
) {
    long hours = seconds / 3600;
    long minutes = (seconds % 3600) / 60;
    long secs = seconds % 60;

    if (hours > 0) {
        snprintf(
            buffer,
            buffer_size,
            "%ldh %02ldm %02lds",
            hours,
            minutes,
            secs
        );
    } else {
        snprintf(
            buffer,
            buffer_size,
            "%ldm %02lds",
            minutes,
            secs
        );
    }
}

static void write_session(
    time_t start,
    time_t finish,
    const WindowInfo *info
) {
    time_t duration = finish - start;

    /*
     * Do not save sessions shorter than one minute.
     */
    if (duration < MIN_SESSION_SECONDS)
        return;

    struct tm start_tm;
    struct tm finish_tm;

    localtime_r(&start, &start_tm);
    localtime_r(&finish, &finish_tm);

    char start_time[32];
    char finish_time[32];

    strftime(
        start_time,
        sizeof(start_time),
        "%H:%M:%S",
        &start_tm
    );

    strftime(
        finish_time,
        sizeof(finish_time),
        "%H:%M:%S",
        &finish_tm
    );

    /*
     * If the session crosses midnight, write it to the
     * day where the session started. Normally midnight
     * is handled before reaching this point.
     */
    FILE *log = open_daily_log(&start_tm);

    if (!log)
        return;

    char duration_string[64];

    format_duration(
        duration,
        duration_string,
        sizeof(duration_string)
    );

    fprintf(
        log,
        "%s -> %s on %s | %s (%s)\n",
        start_time,
        finish_time,
        info->app,
        info->title,
        duration_string
    );

    fflush(log);
    fclose(log);
}

int main(void)
{
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    make_log_directory();

    Display *display = XOpenDisplay(NULL);

    if (!display) {
        fprintf(
            stderr,
            "Could not connect to X11 display.\n"
        );

        return EXIT_FAILURE;
    }
    
    XSetErrorHandler(x11_error_handler);

    Window previous_window = None;

    WindowInfo previous_info;
    memset(&previous_info, 0, sizeof(previous_info));

    time_t session_start = 0;

    while (running) {

        Window active_window = get_active_window(display);

        WindowInfo current_info;
        memset(&current_info, 0, sizeof(current_info));

        int have_window = 0;

        if (active_window != None) {
            have_window = get_window_info(
                display,
                active_window,
                &current_info
            );
        }

        /*
         * We treat a change in window OR title as a new
         * activity session.
         *
         * This means switching browser tabs normally gives
         * you the tab title in the log.
         */
        int changed = 0;

        if (have_window) {

            if (previous_window != active_window) {
                changed = 1;
            } else if (
                strcmp(
                    previous_info.title,
                    current_info.title
                ) != 0
            ) {
                changed = 1;
            }
        } else if (previous_window != None) {
            changed = 1;
        }

        if (changed) {

            if (session_start != 0) {
                time_t now = time(NULL);

                write_session(
                    session_start,
                    now,
                    &previous_info
                );
            }

            if (have_window) {
                session_start = time(NULL);
                previous_window = active_window;

                memcpy(
                    &previous_info,
                    &current_info,
                    sizeof(WindowInfo)
                );
            } else {
                session_start = 0;
                previous_window = None;

                memset(
                    &previous_info,
                    0,
                    sizeof(previous_info)
                );
            }
        }

        /*
         * Midnight handling.
         *
         * If the current session started yesterday,
         * close it at midnight and begin a new session.
         */
        if (session_start != 0 && have_window) {

            time_t now = time(NULL);

            struct tm start_tm;
            struct tm now_tm;

            localtime_r(&session_start, &start_tm);
            localtime_r(&now, &now_tm);

            if (
                start_tm.tm_year != now_tm.tm_year ||
                start_tm.tm_yday != now_tm.tm_yday
            ) {
                write_session(
                    session_start,
                    now,
                    &previous_info
                );

                session_start = now;
            }
        }

        sleep(POLL_INTERVAL);
    }

    /*
     * Save the currently active session when the program
     * exits, provided it lasted at least two minutes.
     */
    if (session_start != 0) {
        time_t now = time(NULL);

        write_session(
            session_start,
            now,
            &previous_info
        );
    }

    XCloseDisplay(display);

    return EXIT_SUCCESS;
}
