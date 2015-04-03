/* vim: set et sw=4 ts=4 sts=4 : */
/********************************************************************\
 * This program is free software; you can redistribute it and/or    *
 * modify it under the terms of the GNU General Public License as   *
 * published by the Free Software Foundation; either version 2 of   *
 * the License, or (at your option) any later version.              *
 *                                                                  *
 * This program is distributed in the hope that it will be useful,  *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of   *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the    *
 * GNU General Public License for more details.                     *
 *                                                                  *
 * You should have received a copy of the GNU General Public License*
 * along with this program; if not, contact:                        *
 *                                                                  *
 * Free Software Foundation           Voice:  +1-617-542-5942       *
 * 59 Temple Place - Suite 330        Fax:    +1-617-542-2652       *
 * Boston, MA  02111-1307,  USA       gnu@gnu.org                   *
 *                                                                  *
\********************************************************************/

/** @file capabilities.h
    @author Copyright (C) 2015 Michael Haas <haas@computerlinguist.org>
*/

#include "../config.h"

#ifdef USE_LIBCAP

#ifndef _CAPABILITIES_H_
#define _CAPABILITIES_H_

void
drop_privileges(const char*, const char*);

void
switch_to_root();

FILE*
popen_as_root(const char*, const char*);

void
set_user_group(const char*, const char*);

void
set_uid_gid(uid_t, gid_t);

#endif                          /* _CAPABILITIES_H_ */

#endif /* USE_LIBCAP */
