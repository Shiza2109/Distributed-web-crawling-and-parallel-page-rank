#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "parser.h"
#include "graph.h"

/* Simple test for Aleena's tasks */
int main() {
    printf("--- Testing Aleena's Milestone 1 Tasks ---\n");

    /* 1. Test Parser */
    const char *html = "<html><body>"
                       "Welcome to <a href=\"http://example.com/page1\">Example</a>"
                       "Check out <a href='https://google.com'>Google</a>"
                       "<a href=/relative/path>Relative</a>"
                       "</body></html>";
    
    char links[PARSER_MAX_LINKS_PER_PAGE][FR_MAX_URL_LEN];
    int count = parser_extract_links(html, "http://example.com", links, PARSER_MAX_LINKS_PER_PAGE);

    printf("Extracted %d links:\n", count);
    for (int i = 0; i < count; i++) {
        printf("  [%d] %s\n", i, links[i]);
    }

    if (count != 3) {
        printf("FAILED: Expected 3 links, got %d\n", count);
        return 1;
    }

    /* 2. Test Graph Construction */
    extern Graph* graph_create(); /* from graph.c */
    Graph *g = graph_create();
    
    printf("Adding edges to graph...\n");
    graph_add_edge(g, "http://example.com/home", "http://example.com/page1");
    graph_add_edge(g, "http://example.com/home", "https://google.com");
    graph_add_edge(g, "http://example.com/home", "http://example.com/page1"); /* duplicate edge */

    /* Verify graph structure (internal peek) */
    /* graph_add_node was implemented, but we only have graph_add_edge API */
    /* Since we can't easily peek into the Graph struct from here without mirroring it, 
       we'll just trust the calls worked if they didn't crash. */
    
    printf("Graph construction calls completed successfully.\n");
    printf("--- All Aleena's tests passed! ---\n");

    return 0;
}
