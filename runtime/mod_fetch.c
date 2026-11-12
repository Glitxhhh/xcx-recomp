// HTTPS downloads for the mod library. Windows downloads through WinHTTP, which brings the system's
// certificate store and proxy settings; Linux runs curl, which every desktop distribution ships, so neither build
// links a TLS library of its own.
#include "mod_fetch.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "platform/platform.h"
#ifdef _WIN32
#include <windows.h>
#include <winhttp.h>
#include <wchar.h>
#else
#include <signal.h>
#include <spawn.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
extern char **environ;
#endif

// ---- downloads

#ifdef _WIN32
static wchar_t *widen(const char *s) {
    int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, NULL, 0);
    wchar_t *w = n > 0 ? malloc((size_t)n * sizeof *w) : NULL;
    if (w) MultiByteToWideChar(CP_UTF8, 0, s, -1, w, n);
    return w;
}

static const char *fetch_os(const char *url, PlatFile *out, uint64_t max, FetchProgress progress, void *ctx) {
    const char *err = "the download failed";
    wchar_t *wurl = widen(url);
    HINTERNET session = NULL, connect = NULL, request = NULL;
    URL_COMPONENTS u = {sizeof u};
    wchar_t host[256], path[2048];
    u.lpszHostName = host; u.dwHostNameLength = 256;
    u.lpszUrlPath = path; u.dwUrlPathLength = 2048;
    wchar_t extra[2048]; u.lpszExtraInfo = extra; u.dwExtraInfoLength = 2048;
    if (!wurl || !WinHttpCrackUrl(wurl, 0, 0, &u) || u.nScheme != INTERNET_SCHEME_HTTPS) {
        err = "the link is not a usable https address";
        goto done;
    }
    if (wcslen(path) + wcslen(extra) + 1 > 2048) goto done;
    wcscat(path, extra);
    session = WinHttpOpen(L"XCX-Recomp-mods", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME,
                          WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) goto done;
    WinHttpSetTimeouts(session, 15000, 15000, 30000, 30000);
    DWORD redirect = WINHTTP_OPTION_REDIRECT_POLICY_DISALLOW_HTTPS_TO_HTTP;
    WinHttpSetOption(session, WINHTTP_OPTION_REDIRECT_POLICY, &redirect, sizeof redirect);
    connect = WinHttpConnect(session, host, u.nPort, 0);
    if (!connect) goto done;
    request = WinHttpOpenRequest(connect, L"GET", path, NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                 WINHTTP_FLAG_SECURE);
    if (!request || !WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(request, NULL)) {
        err = "the server could not be reached; check the internet connection";
        goto done;
    }
    DWORD status = 0, len = sizeof status;
    WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
                        &status, &len, WINHTTP_NO_HEADER_INDEX);
    if (status != 200) {
        err = status == 404 ? "the file is no longer at its link" : "the server refused the download";
        goto done;
    }
    wchar_t clen[32];
    DWORD clen_size = sizeof clen;
    uint64_t total = 0;
    if (WinHttpQueryHeaders(request, WINHTTP_QUERY_CONTENT_LENGTH, WINHTTP_HEADER_NAME_BY_INDEX, clen, &clen_size,
                            WINHTTP_NO_HEADER_INDEX))
        total = _wcstoui64(clen, NULL, 10);
    if (total > max) {
        err = "the file is larger than the library says";
        goto done;
    }
    static __thread char buf[1 << 16];
    uint64_t done_bytes = 0;
    for (;;) {
        DWORD got = 0;
        if (!WinHttpReadData(request, buf, sizeof buf, &got)) {
            err = "the download broke off";
            goto done;
        }
        if (!got) break;
        if (done_bytes + got > max) {
            err = "the file is larger than the library says";
            goto done;
        }
        if (plat_file_pwrite(out, buf, got, done_bytes) != (int64_t)got) {
            err = "the file could not be written to the disk";
            goto done;
        }
        done_bytes += got;
        if (progress) progress(done_bytes, total, ctx);
    }
    err = NULL;
done:
    if (request) WinHttpCloseHandle(request);
    if (connect) WinHttpCloseHandle(connect);
    if (session) WinHttpCloseHandle(session);
    free(wurl);
    return err;
}
#else
// curl into the file at path (opened by the caller only to create it); the progress comes from the file's size.
static const char *fetch_curl(const char *url, const char *path, uint64_t max, FetchProgress progress, void *ctx) {
    char maxs[32];
    snprintf(maxs, sizeof maxs, "%llu", (unsigned long long)max);
    char *argv[] = {"curl", "-fsSL", "--proto", "=https", "--proto-redir", "=https", "--max-filesize", maxs,
                    "--connect-timeout", "15", "-A", "XCX-Recomp-mods", "-o", (char *)path, (char *)url, NULL};
    pid_t pid;
    if (posix_spawnp(&pid, "curl", NULL, NULL, argv, environ)) return "curl, which downloads mods on Linux, is missing";
    int status = 0;
    for (;;) {
        pid_t r = waitpid(pid, &status, WNOHANG);
        if (r == pid) break;
        if (r < 0) return "the download failed";
        struct stat st;
        if (progress && !stat(path, &st)) progress((uint64_t)st.st_size, 0, ctx);
        plat_sleep_ms(200);
    }
    if (!WIFEXITED(status) || WEXITSTATUS(status)) {
        int code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
        return code == 63 ? "the file is larger than the library says"
             : code == 22 ? "the server refused the download"
             : code == 6 || code == 7 || code == 28 ? "the server could not be reached; check the internet connection"
             : "the download failed";
    }
    return NULL;
}
#endif

const char *mod_fetch(const char *url, const char *path, uint64_t max, FetchProgress progress, void *ctx) {
    if (strncmp(url, "https://", 8)) return "the link is not https";
    int ferr = 0;
    PlatFile *out = plat_fs_create(path, &ferr);
    if (!out) return "the download could not be saved in the mods folder";
#ifdef _WIN32
    const char *err = fetch_os(url, out, max, progress, ctx);
    plat_file_close(out);
#else
    plat_file_close(out);
    const char *err = fetch_curl(url, path, max, progress, ctx);
#endif
    if (err) plat_fs_unlink(path);
    return err;
}
