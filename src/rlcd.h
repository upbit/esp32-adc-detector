#pragma once

#include <stdint.h>

#define RLCD_W 400  // 横屏
#define RLCD_H 300

void rlcd_init(void);
// L8 灰度帧（RLCD_W × RLCD_H），按阈值转 1bpp 后整屏发送
void rlcd_draw_l8(const uint8_t *px);
