/* test_mocks.h - Write this to test YOUR code */
#ifndef TEST_MOCKS_H
#define TEST_MOCKS_H

/* Mock Aleena's parser output */
char** mock_parser_extract_links(const char* html) {
    static char* mock_links[] = {
        "http://example.com/page1",
        "http://example.com/page2",
        NULL
    };
    return mock_links;
}

/* Mock Abdur's duplicate filter */
int mock_is_duplicate(const char* url) {
    /* Always return 0 (not duplicate) for testing */
    return 0;
}

#endif