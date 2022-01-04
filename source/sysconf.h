/* 
 * Copyright (c) 2015-2022, Extrems' Corner.org
 * 
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#ifndef GBI_SYSCONF_H
#define GBI_SYSCONF_H

#include <ogc/conf.h>
#include <ogc/system.h>

#ifdef HW_DOL
#define SYSCONF_VIDEO_NTSC           SYS_VIDEO_NTSC
#define SYSCONF_VIDEO_PAL            SYS_VIDEO_PAL
#define SYSCONF_VIDEO_MPAL           SYS_VIDEO_MPAL

#define SYSCONF_GetProgressiveScan() SYS_GetProgressiveScan()
#define SYSCONF_GetVideoMode()       SYS_GetVideoMode()
#define SYSCONF_GetEuRGB60()         SYS_GetEuRGB60()
#define SYSCONF_GetDisplayOffsetH()  SYS_GetDisplayOffsetH()
#else
#define SYSCONF_VIDEO_NTSC           CONF_VIDEO_NTSC
#define SYSCONF_VIDEO_PAL            CONF_VIDEO_PAL
#define SYSCONF_VIDEO_MPAL           CONF_VIDEO_MPAL

#define SYSCONF_GetProgressiveScan() CONF_GetProgressiveScan() > CONF_ERR_OK
#define SYSCONF_GetVideoMode()       CONF_GetVideo()
#define SYSCONF_GetEuRGB60()         CONF_GetEuRGB60() > CONF_ERR_OK
#define SYSCONF_GetDisplayOffsetH()  ({ int8_t offset; CONF_GetDisplayOffsetH(&offset); offset; })
#endif

#endif /* GBI_SYSCONF_H */
