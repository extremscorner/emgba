/* 
 * Copyright (c) 2015-2021, Extrems' Corner.org
 * 
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#ifndef GBI_GBP_H
#define GBI_GBP_H

#include <stdint.h>

int GBPGetController(void);
int GBPGetScreenSize(void);
int GBPGetFrame(void);
int GBPGetTimer(void);
int GBPGetScreenFilter(void);

#endif /* GBI_GBP_H */
