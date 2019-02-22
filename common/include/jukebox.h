/*
 * This file is part of the DXX-Rebirth project <https://www.dxx-rebirth.com/>.
 * It is copyright by its individual contributors, as recorded in the
 * project's Git history.  See COPYING.txt at the top level for license
 * terms and a link to the Git history.
 */

#pragma once

#include "physfsx.h"

#ifdef __cplusplus

#ifdef dsx
namespace dcx {
extern const array<file_extension_t, 5> jukebox_exts;

void jukebox_unload();
}
namespace dsx {
void jukebox_load();
int jukebox_play();
}
#endif

#endif
