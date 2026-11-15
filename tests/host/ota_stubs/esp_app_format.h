// SPDX-License-Identifier: MIT
#pragma once

struct esp_image_header_t {
    unsigned char bytes[24];
};

struct esp_image_segment_header_t {
    unsigned char bytes[8];
};
