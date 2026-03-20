#include "parser.h"
#include <string.h>
#include <ctype.h>
#include <stdio.h>

// Case-insensitive substring search 
const char* my_strcasestr(const char* haystack, const char* needle) {
    if (!*needle) return haystack;
    for (; *haystack; haystack++) {
        if (tolower(*haystack) == tolower(*needle)) {
            const char *h, *n;
            for (h = haystack, n = needle; *h && *n; h++, n++) {
                if (tolower(*h) != tolower(*n)) break;
            }
            if (!*n) return haystack;
        }
    }
    return NULL;
}

int parser_extract_links(const char *html, const char *base_url,
                         char out_links[][FR_MAX_URL_LEN], int max_links) {
    if (!html) return 0;

    int count = 0;
    const char *p = html;

    while (count < max_links) {
        // Look for "<a" 
        p = my_strcasestr(p, "<a");
        if (!p) break;
        p += 2;

        /* Look for "href=" inside the tag */
        const char *href = my_strcasestr(p, "href=");
        if (!href) {
            p += 2; /* Advance to avoid infinite loop */
            continue;
        }

        // Check if we are still inside the <a> tag (not perfect, but simple) 
        const char *tag_end = strchr(p, '>');
        if (tag_end && href > tag_end) continue;

        href += 5; //skip href

        // Handle quotes if present 
        char quote = 0;
        if (*href == '\"' || *href == '\'') {
            quote = *href++;
        }

        // Extract the URL
        char *dst = out_links[count];
        int len = 0;
        while (*href && len < FR_MAX_URL_LEN - 1) {
            if (quote) {
                if (*href == quote) break;
            } else {
                if (isspace(*href) || *href == '>') break;
            }
            *dst++ = *href++;
            len++;
        }
        *dst = '\0';

        /* Skip if empty URL or non-crawlable (javascript, mailto, etc) */
        if (len > 0) {
            if (strncmp(out_links[count], "javascript:", 11) == 0 ||
                strncmp(out_links[count], "mailto:", 7) == 0 ||
                strncmp(out_links[count], "tel:", 4) == 0 ||
                out_links[count][0] == '#') {
                /* Skip this link */
            } else {
                count++;
            }
        }

        p = href;
    }

    return count;
}
