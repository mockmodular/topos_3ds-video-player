#if !defined(DEF_VID_SCREEN_HPP)
#define DEF_VID_SCREEN_HPP

#include <stdbool.h>
#include <stdint.h>

#include "vid_state.h"

/** 显示用宽高：先取 Media_v_info.width/height（零则用 codec_*），再乘 SAR（sar_width/sar_height 为对 coded 尺寸的乘子）。
 *  用于 2D/3D 自动判定、tex 过滤、像素完美、上屏 fit，与软/硬解裁切后的绘制比例一致。 */
void Vid_video_presentation_wh(Vid_eye eye, uint32_t *out_w, uint32_t *out_h);

void Vid_fit_to_screen(uint16_t screen_width, uint16_t screen_height, Vid_eye eye_index);
/** True when Video > Scale is pixel-perfect, 2D, and dimensions match rules — forces NEAREST over tex filter. */
bool Vid_pixel_perfect_forces_nearest(uint32_t eye_k);
void Vid_change_video_size(double change_px, Vid_eye eye_index);

/** 读 Sem：底屏亮=true / 息=false（唯一真相 `Sem_config.is_bottom_lcd_on`）。 */
bool Vid_bottom_lcd_lit(void);
/** Player 且底屏息：上屏铺纯黑（观影）；其余面板恒 false。 */
bool Vid_player_top_should_fill_black(void);

/** 唯一开关写 false：息底（含自动 5 秒）。 */
void Vid_enter_full_screen(void);
/** 唯一开关写 true：亮底。 */
void Vid_exit_full_screen(void);
/** Player：翻转该开关（Select / 空白区）。 */
void Vid_toggle_bottom_lcd_player(void);
/** 设置里自动息屏开启时：Player 亮底约 5s 无操作 → Vid_enter_full_screen。 */
void Vid_control_full_screen(void);

#endif //!defined(DEF_VID_SCREEN_HPP)
