/* fetch.c */

#include "fetch.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <curl/curl.h>

/* ── write callback: appends curl data chunks into a growing MemBuf ── */
typedef struct {
    char  *buf;
    size_t size;
    size_t limit;
} MemBuf;

static size_t write_cb(void *data, size_t sz, size_t nmemb, void *userp) {
    size_t real = sz * nmemb;
    MemBuf *m   = userp;

    /* enforce response size cap */
    if (m->limit > 0 && m->size + real > m->limit) {
        real = m->limit - m->size;
        if (real == 0) return 0;          /* signals curl to abort      */
    }

    char *tmp = realloc(m->buf, m->size + real + 1);
    if (!tmp) return 0;                   /* OOM — abort transfer       */
    m->buf = tmp;
    memcpy(m->buf + m->size, data, real);
    m->size        += real;
    m->buf[m->size] = '\0';
    return real;
}

/* ── header callback: capture Content-Type ── */
typedef struct {
    char content_type[128];
    int  http_code;           /* read from status line as fallback          */
} HeaderCtx;

static size_t header_cb(char *line, size_t sz, size_t nmemb, void *userp) {
    size_t len = sz * nmemb;
    HeaderCtx *h = userp;

    /* Content-Type: text/html; charset=utf-8 */
    if (len > 13 && strncasecmp(line, "Content-Type:", 13) == 0) {
        const char *val = line + 13;
        while (*val == ' ') val++;
        /* copy until CRLF */
        size_t vlen = strcspn(val, "\r\n");
        if (vlen >= sizeof(h->content_type))
            vlen = sizeof(h->content_type) - 1;
        strncpy(h->content_type, val, vlen);
        h->content_type[vlen] = '\0';
    }
    return len;
}

/* ── map HTTP status code to FetchOutcome ── */
static FetchOutcome outcome_from_http(int code) {
    if (code == 200)                     return FETCH_OK;
    if (code == 301 || code == 302 ||
        code == 303 || code == 307 ||
        code == 308)                     return FETCH_REDIRECT;
    if (code == 403)                     return FETCH_FORBIDDEN;
    if (code == 404)                     return FETCH_NOT_FOUND;
    if (code == 410)                     return FETCH_GONE;
    if (code == 429)                     return FETCH_SERVER_ERROR; /* rate-limited — retry */
    if (code >= 500 && code < 600)       return FETCH_SERVER_ERROR;
    if (code == 0)                       return FETCH_TIMEOUT;
    return FETCH_ERROR;
}

/* ── check if content-type is HTML ── */
static bool is_html(const char *ct) {
    return (strstr(ct, "text/html") != NULL ||
            strstr(ct, "application/xhtml") != NULL);
}

/* ═══════════════════════════════════════════════════
   Public API
   ═══════════════════════════════════════════════════ */

void fetch_global_init(void) {
    curl_global_init(CURL_GLOBAL_ALL);
}

void fetch_global_cleanup(void) {
    curl_global_cleanup();
}

void fetch_url(const char *url, const char *user_agent, FetchResult *result) {
    /* zero result */
    memset(result, 0, sizeof(*result));
    strncpy(result->url, url, FR_MAX_URL_LEN - 1);

    CURL *curl = curl_easy_init();
    if (!curl) {
        result->outcome = FETCH_ERROR;
        return;
    }

    /* body buffer */
    MemBuf body = {
        .buf   = malloc(1),
        .size  = 0,
        .limit = FETCH_MAX_RESPONSE_BYTES,
    };
    if (!body.buf) {
        result->outcome = FETCH_ERROR;
        curl_easy_cleanup(curl);
        return;
    }
    body.buf[0] = '\0';

    /* header context */
    HeaderCtx hctx;
    memset(&hctx, 0, sizeof(hctx));

    /* ── curl options ── */
    curl_easy_setopt(curl, CURLOPT_URL,              url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION,    write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA,        &body);
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION,   header_cb);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA,       &hctx);
    curl_easy_setopt(curl, CURLOPT_USERAGENT,        user_agent
                                                      ? user_agent
                                                      : "PDC-Crawler/1.0");
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION,   0L); /* we handle redirects manually */
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT,   (long)FETCH_CONNECT_TIMEOUT_SEC);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT,          (long)FETCH_TOTAL_TIMEOUT_SEC);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS,        0L);
    curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING,  "gzip, deflate"); /* auto-decompress */
    curl_easy_setopt(curl, CURLOPT_TCP_KEEPALIVE,    1L);

    /* SSL: verify cert by default; set to 0 only for testing */
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER,   1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST,   2L);

    /* ── perform ── */
    CURLcode res = curl_easy_perform(curl);

    /* ── read final URL (even without following redirects, curl fills this) ── */
    char *effective = NULL;
    curl_easy_getinfo(curl, CURLINFO_EFFECTIVE_URL,    &effective);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE,    &result->http_code);

    if (effective && strcmp(effective, url) != 0)
        strncpy(result->url, effective, FR_MAX_URL_LEN - 1);

    curl_easy_cleanup(curl);

    /* ── interpret curl error ── */
    if (res == CURLE_OPERATION_TIMEDOUT || res == CURLE_COULDNT_CONNECT) {
        result->outcome = FETCH_TIMEOUT;
        free(body.buf);
        return;
    }
    if (res != CURLE_OK && res != CURLE_WRITE_ERROR) {
        /* CURLE_WRITE_ERROR fires when write_cb returns 0 (size cap hit) —
           that's intentional, treat it as a truncated-but-ok response      */
        result->outcome = FETCH_ERROR;
        free(body.buf);
        return;
    }

    /* ── interpret HTTP status ── */
    result->outcome = outcome_from_http(result->http_code);

    /* for redirects we keep body empty — frontier handles the Location URL  */
    if (result->outcome == FETCH_REDIRECT) {
        free(body.buf);
        return;
    }

    /* ── content-type check ── */
    if (result->outcome == FETCH_OK && !is_html(hctx.content_type)) {
        result->outcome = FETCH_CONTENT_SKIP;
        free(body.buf);
        return;
    }

    /* ── attach body on success ── */
    if (result->outcome == FETCH_OK) {
        result->html     = body.buf;
        result->html_len = body.size;
    } else {
        free(body.buf);
    }
}

void fetch_result_free(FetchResult *result) {
    if (result && result->html) {
        free(result->html);
        result->html     = NULL;
        result->html_len = 0;
    }
}