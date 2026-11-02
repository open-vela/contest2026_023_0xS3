/****************************************************************************
 * VelaOps 资源看板纯渲染接口。
 *
 * 该层只负责把状态绘制到 RGB565 像素缓冲区，不依赖 LCD 驱动，便于在
 * 开发机上测试和预览，也为后续替换为 LVGL 保留清晰边界。
 ****************************************************************************/

#ifndef VELAOPS_DASHBOARD_H
#define VELAOPS_DASHBOARD_H

#include <stddef.h>
#include <stdint.h>

#include "velaops_display.h"

int velaops_dashboard_render(uint16_t *pixels, size_t pixel_count,
                             const velaops_display_state_t *state,
                             unsigned int page);
int velaops_dashboard_render_test(uint16_t *pixels, size_t pixel_count);

/* 把本轮的 CPU 使用率追加进状态里的历史环形缓冲，供性能页曲线使用。
 * 非有效 CPU 数据会被忽略。 */
void velaops_dashboard_push_cpu(velaops_display_state_t *state);

/* 提示框渲染：居中消息框，边框颜色随 blink_phase 交替实现闪烁提示，
 * 仅支持 ASCII 文本（屏显字库为 5x7 ASCII 点阵）。 */
int velaops_dashboard_render_message(uint16_t *pixels, size_t pixel_count,
                                     const char *title, const char *text,
                                     int blink_phase);

#endif /* VELAOPS_DASHBOARD_H */
