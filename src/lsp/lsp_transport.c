#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "lsp_transport.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- Content-Length Framer ---- */

void mc_lsp_framer_init(mc_lsp_framer *f)
{
    if (f == NULL) return;
    memset(f, 0, sizeof(*f));
    mc_strbuf_init(&f->buf);
}

void mc_lsp_framer_destroy(mc_lsp_framer *f)
{
    if (f == NULL) return;
    mc_strbuf_destroy(&f->buf);
    memset(f, 0, sizeof(*f));
}

static int mc_strncasecmp_ascii(const char *a, const char *b, size_t n)
{
    size_t i;
    for (i = 0; i < n; ++i) {
        int ca = tolower((unsigned char)a[i]);
        int cb = tolower((unsigned char)b[i]);
        if (ca != cb) return ca - cb;
    }
    return 0;
}

static void mc_strbuf_consume(mc_strbuf *sb, size_t n)
{
    if (n == 0 || sb->length == 0) return;
    if (n >= sb->length) {
        mc_strbuf_clear(sb);
        return;
    }
    memmove(sb->data, sb->data + n, sb->length - n);
    sb->length -= n;
    sb->data[sb->length] = '\0';
}

static bool mc_parse_headers(const char *hdr, size_t hdr_len, size_t *out_cl)
{
    size_t pos = 0;
    bool found = false;
    while (pos < hdr_len) {
        size_t line_end = pos;
        while (line_end < hdr_len && hdr[line_end] != '\r' && hdr[line_end] != '\n')
            ++line_end;
        if (line_end - pos > 15 &&
            mc_strncasecmp_ascii(hdr + pos, "Content-Length:", 15) == 0) {
            size_t p = pos + 15;
            size_t val = 0;
            bool has_digit = false;
            while (p < line_end && (hdr[p] == ' ' || hdr[p] == '\t')) ++p;
            while (p < line_end && hdr[p] >= '0' && hdr[p] <= '9') {
                val = val * 10u + (size_t)(hdr[p] - '0');
                has_digit = true;
                ++p;
            }
            if (has_digit) {
                *out_cl = val;
                found = true;
            }
        }
        pos = line_end;
        if (pos < hdr_len && hdr[pos] == '\r') ++pos;
        if (pos < hdr_len && hdr[pos] == '\n') ++pos;
    }
    return found;
}

int mc_lsp_framer_feed(mc_lsp_framer *f, const char *data, size_t len,
                       mc_lsp_frame_cb on_frame, void *userdata)
{
    if (f == NULL || (data == NULL && len != 0)) return 0;
    if (len > 0 && !mc_strbuf_append(&f->buf, data, len)) return 0;

    for (;;) {
        if (!f->in_body) {
            size_t i;
            size_t sep = SIZE_MAX;
            size_t sep_len = 0;
            if (f->buf.length < 4) break;
            for (i = 0; i + 3 < f->buf.length; ++i) {
                if (f->buf.data[i] == '\r' && f->buf.data[i + 1] == '\n' &&
                    f->buf.data[i + 2] == '\r' && f->buf.data[i + 3] == '\n') {
                    sep = i;
                    sep_len = 4;
                    break;
                }
            }
            if (sep == SIZE_MAX) {
                /* Also tolerate \n\n */
                for (i = 0; i + 1 < f->buf.length; ++i) {
                    if (f->buf.data[i] == '\n' && f->buf.data[i + 1] == '\n') {
                        sep = i;
                        sep_len = 2;
                        break;
                    }
                }
            }
            if (sep == SIZE_MAX) break;
            {
                size_t cl = 0;
                bool ok = mc_parse_headers(f->buf.data, sep, &cl);
                mc_strbuf_consume(&f->buf, sep + sep_len);
                if (!ok) return 0;
                f->content_length = cl;
                f->in_body = true;
            }
        }
        if (f->in_body) {
            if (f->buf.length < f->content_length) break;
            if (on_frame != NULL) {
                on_frame(userdata, f->buf.data, f->content_length);
            }
            mc_strbuf_consume(&f->buf, f->content_length);
            f->in_body = false;
            f->content_length = 0;
        }
    }
    return 1;
}

/* ---- Subprocess + Pipes + Job Object + 3 Win32 Threads ---- */

struct mc_lsp_transport {
    HANDLE job;
    HANDLE process;
    HANDLE h_stdin_write;
    HANDLE h_stdout_read;
    HANDLE h_stderr_read;
    HANDLE write_thread;
    HANDLE read_thread;
    HANDLE stderr_thread;
    mc_lsp_queue *send_q;
    mc_lsp_queue *recv_q;
    volatile LONG alive;
    void (*on_notify)(void *ud);
    void *on_notify_ud;
    void (*on_stderr_line)(void *ud, const char *line);
    void *on_stderr_ud;
    void (*on_exit)(void *ud, uint32_t exit_code);
    void *on_exit_ud;
};

static int mc_write_all_pipe(HANDLE h, const char *data, size_t len)
{
    size_t sent = 0;
    while (sent < len) {
        DWORD chunk = (len - sent) > 65536u ? 65536u : (DWORD)(len - sent);
        DWORD written = 0;
        if (!WriteFile(h, data + sent, chunk, &written, NULL) || written == 0)
            return 0;
        sent += written;
    }
    return 1;
}

static DWORD WINAPI mc_lsp_write_thread_proc(void *arg)
{
    mc_lsp_transport *tr = (mc_lsp_transport *)arg;
    mc_lsp_message *msg = NULL;
    while (mc_lsp_queue_pop(tr->send_q, MC_LSP_QUEUE_WAIT_FOREVER, &msg) == MC_LSP_QUEUE_ITEM) {
        char hdr[64];
        size_t len = mc_lsp_message_length(msg);
        int hlen = snprintf(hdr, sizeof(hdr), "Content-Length: %zu\r\n\r\n", len);
        int ok = 0;
        if (hlen > 0) {
            ok = mc_write_all_pipe(tr->h_stdin_write, hdr, (size_t)hlen) &&
                 mc_write_all_pipe(tr->h_stdin_write,
                                   (const char *)mc_lsp_message_data(msg), len);
        }
        mc_lsp_message_free(msg);
        msg = NULL;
        if (!ok) break;
    }
    return 0;
}

static void mc_on_framed_payload(void *ud, const char *payload, size_t len)
{
    mc_lsp_transport *tr = (mc_lsp_transport *)ud;
    if (mc_lsp_queue_push(tr->recv_q, payload, len)) {
        if (tr->on_notify != NULL) tr->on_notify(tr->on_notify_ud);
    }
}

static DWORD WINAPI mc_lsp_read_thread_proc(void *arg)
{
    mc_lsp_transport *tr = (mc_lsp_transport *)arg;
    mc_lsp_framer framer;
    char buf[8192];
    DWORD got = 0;

    mc_lsp_framer_init(&framer);
    while (ReadFile(tr->h_stdout_read, buf, sizeof(buf), &got, NULL) && got > 0) {
        if (!mc_lsp_framer_feed(&framer, buf, (size_t)got,
                                mc_on_framed_payload, tr)) {
            break;
        }
    }
    mc_lsp_framer_destroy(&framer);

    InterlockedExchange(&tr->alive, 0);
    mc_lsp_queue_close(tr->recv_q);
    if (tr->on_exit != NULL) {
        DWORD code = 0;
        (void)WaitForSingleObject(tr->process, 200);
        (void)GetExitCodeProcess(tr->process, &code);
        tr->on_exit(tr->on_exit_ud, (uint32_t)code);
    }
    if (tr->on_notify != NULL) tr->on_notify(tr->on_notify_ud);
    return 0;
}

static DWORD WINAPI mc_lsp_stderr_thread_proc(void *arg)
{
    mc_lsp_transport *tr = (mc_lsp_transport *)arg;
    mc_strbuf line;
    char buf[4096];
    DWORD got = 0;
    mc_strbuf_init(&line);

    while (ReadFile(tr->h_stderr_read, buf, sizeof(buf), &got, NULL) && got > 0) {
        DWORD i;
        for (i = 0; i < got; ++i) {
            if (buf[i] == '\n') {
                if (line.length > 0 && line.data[line.length - 1] == '\r') {
                    --line.length;
                    line.data[line.length] = '\0';
                }
                if (tr->on_stderr_line != NULL && line.length > 0) {
                    tr->on_stderr_line(tr->on_stderr_ud, line.data);
                }
                mc_strbuf_clear(&line);
            } else if (line.length < 4096) {
                (void)mc_strbuf_append_char(&line, buf[i]);
            }
        }
    }
    if (tr->on_stderr_line != NULL && line.length > 0) {
        tr->on_stderr_line(tr->on_stderr_ud, line.data);
    }
    mc_strbuf_destroy(&line);
    return 0;
}

mc_lsp_transport *mc_lsp_transport_start(const mc_lsp_transport_cfg *cfg)
{
    mc_lsp_transport *tr;
    SECURITY_ATTRIBUTES sa;
    HANDLE stdin_r = NULL, stdin_w = NULL;
    HANDLE stdout_r = NULL, stdout_w = NULL;
    HANDLE stderr_r = NULL, stderr_w = NULL;
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION jeli;
    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    wchar_t *cmd_copy = NULL;
    size_t cmd_len;

    if (cfg == NULL || cfg->command_line == NULL || cfg->command_line[0] == L'\0')
        return NULL;

    tr = (mc_lsp_transport *)calloc(1, sizeof(*tr));
    if (tr == NULL) return NULL;
    tr->on_notify = cfg->on_notify;
    tr->on_notify_ud = cfg->on_notify_ud;
    tr->on_stderr_line = cfg->on_stderr_line;
    tr->on_stderr_ud = cfg->on_stderr_ud;
    tr->on_exit = cfg->on_exit;
    tr->on_exit_ud = cfg->on_exit_ud;

    tr->send_q = mc_lsp_queue_create();
    tr->recv_q = mc_lsp_queue_create();
    if (tr->send_q == NULL || tr->recv_q == NULL) goto fail;

    memset(&sa, 0, sizeof(sa));
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    if (!CreatePipe(&stdin_r, &stdin_w, &sa, 0)) goto fail;
    if (!CreatePipe(&stdout_r, &stdout_w, &sa, 0)) goto fail;
    if (!CreatePipe(&stderr_r, &stderr_w, &sa, 0)) goto fail;

    SetHandleInformation(stdin_w, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(stdout_r, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(stderr_r, HANDLE_FLAG_INHERIT, 0);

    tr->job = CreateJobObjectW(NULL, NULL);
    if (tr->job != NULL) {
        memset(&jeli, 0, sizeof(jeli));
        jeli.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        (void)SetInformationJobObject(tr->job, JobObjectExtendedLimitInformation,
                                      &jeli, sizeof(jeli));
    }

    cmd_len = wcslen(cfg->command_line);
    cmd_copy = (wchar_t *)malloc((cmd_len + 1) * sizeof(wchar_t));
    if (cmd_copy == NULL) goto fail;
    memcpy(cmd_copy, cfg->command_line, (cmd_len + 1) * sizeof(wchar_t));

    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = stdin_r;
    si.hStdOutput = stdout_w;
    si.hStdError = stderr_w;
    memset(&pi, 0, sizeof(pi));

    if (!CreateProcessW(NULL, cmd_copy, NULL, NULL, TRUE,
                        CREATE_SUSPENDED | CREATE_NO_WINDOW,
                        NULL,
                        (cfg->working_dir && cfg->working_dir[0]) ? cfg->working_dir : NULL,
                        &si, &pi)) {
        free(cmd_copy);
        goto fail;
    }
    free(cmd_copy);

    if (tr->job != NULL) {
        (void)AssignProcessToJobObject(tr->job, pi.hProcess);
    }
    ResumeThread(pi.hThread);
    CloseHandle(pi.hThread);

    CloseHandle(stdin_r);  stdin_r = NULL;
    CloseHandle(stdout_w); stdout_w = NULL;
    CloseHandle(stderr_w); stderr_w = NULL;

    tr->process = pi.hProcess;
    tr->h_stdin_write = stdin_w;
    tr->h_stdout_read = stdout_r;
    tr->h_stderr_read = stderr_r;
    InterlockedExchange(&tr->alive, 1);

    tr->write_thread = CreateThread(NULL, 0, mc_lsp_write_thread_proc, tr, 0, NULL);
    tr->read_thread = CreateThread(NULL, 0, mc_lsp_read_thread_proc, tr, 0, NULL);
    tr->stderr_thread = CreateThread(NULL, 0, mc_lsp_stderr_thread_proc, tr, 0, NULL);
    if (tr->write_thread == NULL || tr->read_thread == NULL || tr->stderr_thread == NULL) {
        mc_lsp_transport_stop(tr, 500);
        mc_lsp_transport_destroy(tr);
        return NULL;
    }
    return tr;

fail:
    if (stdin_r) CloseHandle(stdin_r);
    if (stdin_w) CloseHandle(stdin_w);
    if (stdout_r) CloseHandle(stdout_r);
    if (stdout_w) CloseHandle(stdout_w);
    if (stderr_r) CloseHandle(stderr_r);
    if (stderr_w) CloseHandle(stderr_w);
    if (tr->job) CloseHandle(tr->job);
    mc_lsp_queue_destroy(tr->send_q);
    mc_lsp_queue_destroy(tr->recv_q);
    free(tr);
    return NULL;
}

int mc_lsp_transport_send(mc_lsp_transport *tr, const char *json, size_t len)
{
    if (tr == NULL || json == NULL || len == 0) return 0;
    return mc_lsp_queue_push(tr->send_q, json, len);
}

mc_lsp_queue_result mc_lsp_transport_pop(mc_lsp_transport *tr, uint32_t timeout_ms,
                                         mc_lsp_message **out_msg)
{
    if (tr == NULL) return MC_LSP_QUEUE_ERROR;
    return mc_lsp_queue_pop(tr->recv_q, timeout_ms, out_msg);
}

bool mc_lsp_transport_is_alive(const mc_lsp_transport *tr)
{
    return tr != NULL && InterlockedCompareExchange((volatile LONG *)&tr->alive, 0, 0) != 0;
}

bool mc_lsp_transport_stop(mc_lsp_transport *tr, uint32_t timeout_ms)
{
    bool graceful = false;
    if (tr == NULL) return false;
    mc_lsp_queue_close(tr->send_q);
    if (tr->write_thread != NULL) {
        (void)WaitForSingleObject(tr->write_thread, 500);
    }
    if (tr->h_stdin_write != NULL) {
        CloseHandle(tr->h_stdin_write);
        tr->h_stdin_write = NULL;
    }
    if (tr->process != NULL) {
        DWORD wr = WaitForSingleObject(tr->process, timeout_ms);
        if (wr == WAIT_OBJECT_0) {
            graceful = true;
        } else {
            if (tr->job != NULL) {
                (void)TerminateJobObject(tr->job, 1);
            } else {
                (void)TerminateProcess(tr->process, 1);
            }
            (void)WaitForSingleObject(tr->process, 2000);
        }
    }
    InterlockedExchange(&tr->alive, 0);
    mc_lsp_queue_close(tr->recv_q);
    if (tr->read_thread != NULL) (void)WaitForSingleObject(tr->read_thread, 2000);
    if (tr->stderr_thread != NULL) (void)WaitForSingleObject(tr->stderr_thread, 2000);
    if (tr->write_thread != NULL) (void)WaitForSingleObject(tr->write_thread, 1000);
    return graceful;
}

void mc_lsp_transport_destroy(mc_lsp_transport *tr)
{
    if (tr == NULL) return;
    if (mc_lsp_transport_is_alive(tr)) {
        (void)mc_lsp_transport_stop(tr, 500);
    }
    if (tr->write_thread) CloseHandle(tr->write_thread);
    if (tr->read_thread) CloseHandle(tr->read_thread);
    if (tr->stderr_thread) CloseHandle(tr->stderr_thread);
    if (tr->h_stdin_write) CloseHandle(tr->h_stdin_write);
    if (tr->h_stdout_read) CloseHandle(tr->h_stdout_read);
    if (tr->h_stderr_read) CloseHandle(tr->h_stderr_read);
    if (tr->process) CloseHandle(tr->process);
    if (tr->job) CloseHandle(tr->job);
    mc_lsp_queue_destroy(tr->send_q);
    mc_lsp_queue_destroy(tr->recv_q);
    free(tr);
}
