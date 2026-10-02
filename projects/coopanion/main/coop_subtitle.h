// SPDX-License-Identifier: MIT
#pragma once
#include <stddef.h>
#define COOP_SUBTITLE_PAGE_BYTES 256
unsigned coop_subtitle_pages(const char *text);
void coop_subtitle_page(const char *text, unsigned page, char out[COOP_SUBTITLE_PAGE_BYTES]);
