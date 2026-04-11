#if !defined(DEF_VID_SEEK_ENGINE_H)
#define DEF_VID_SEEK_ENGINE_H

#include <stdbool.h>
#include <stdint.h>

//Read-only snapshot consumed by UI layers (draw, HUD) to display the seek bar.
//All positions are in milliseconds.
typedef struct
{
	double	display_pos_ms;	//Position the bar indicator should show.
	double	duration_ms;
	bool	is_seeking;		//PLAYER_STATE_SEEKING or PLAYER_STATE_PREPARE_SEEKING.
	bool	is_drag_active;	//User is currently dragging the seek bar.
} VidSeekEngineView;

//──UI → Engine────────────────────────────────────────────────────────────────
//User is dragging; touch_x is the raw touch pixel X coordinate.
void VidSeekEngine_on_bar_drag(int16_t touch_x);

//User released the bar; commit the previewed position as the seek target.
void VidSeekEngine_on_bar_commit(void);

//Step forward / backward by seek_duration seconds (d-pad / button).
void VidSeekEngine_on_step_fwd(void);
void VidSeekEngine_on_step_back(void);

//──Engine → UI────────────────────────────────────────────────────────────────
//Returns the current display state; safe to call every frame from the draw path.
VidSeekEngineView VidSeekEngine_get_view(void);

#endif //!defined(DEF_VID_SEEK_ENGINE_H)
