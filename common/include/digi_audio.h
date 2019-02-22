/*
 * This file is part of the DXX-Rebirth project <https://www.dxx-rebirth.com/>.
 * It is copyright by its individual contributors, as recorded in the
 * project's Git history.  See COPYING.txt at the top level for license
 * terms and a link to the Git history.
 */

#pragma once

#include "maths.h"

#ifdef __cplusplus

#ifdef dsx
namespace dcx {
struct sound_object;
constexpr std::integral_constant<int, 16> digi_max_channels{};
}
namespace dsx {
int digi_audio_init();
void digi_audio_reset();
void digi_audio_close();
void digi_audio_stop_all_channels();
int digi_audio_start_sound(short, fix, int, int, int, int, sound_object *);
int digi_audio_is_channel_playing(int );
void digi_audio_set_channel_volume(int, int );
void digi_audio_set_channel_pan(int, int );
void digi_audio_stop_sound(int );
void digi_audio_end_sound(int );
void digi_audio_set_digi_volume(int);
}
#endif

#endif
