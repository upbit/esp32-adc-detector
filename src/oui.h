#pragma once

#include <stdint.h>

// 可疑厂商查询：命中返回厂商名，否则返回 NULL
const char *oui_lookup(const uint8_t mac[6]);
