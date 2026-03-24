/* parser.h - Aleena's task: HTML link extraction */
#ifndef PARSER_H
#define PARSER_H

#define PARSER_MAX_LINKS_PER_PAGE 256
#define FR_MAX_URL_LEN            4096

/**
 * parser_extract_links - find <a href="..."> tags in HTML
 * @html: raw HTML content
 * @base_url: parent URL for resolving relative links (placeholder for now)
 * @out_links: buffer to store extracted link URLs
 * @max_links: capacity of out_links
 * 
 * Returns the number of links found.
 */
int parser_extract_links(const char *html, const char *base_url,
                         char out_links[][FR_MAX_URL_LEN], int max_links);

#endif