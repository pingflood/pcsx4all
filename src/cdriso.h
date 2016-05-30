/***************************************************************************
 *   Copyright (C) 2007 PCSX-df Team                                       *
 *   Copyright (C) 2009 Wei Mingzhi                                        *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   51 Franklin Street, Fifth Floor, Boston, MA 02111-1307 USA.           *
 ***************************************************************************/

#ifndef CDRISO_H
#define CDRISO_H

void cdrIsoInit(void);
int cdrIsoActive(void);

///////////////////////////////////////////////////////////////////////////////
//senquack - Functions added for ECM support (cdrecm.cpp)

// If a CD image decoder is asked to read non-encoded tracks in a CUE file,
//  i.e. separate audio tracks, it needs access to the normal read function:
// TODO: make code more intelligent so it doesn't ask decoder functions
//  to read non-encoded sectors
int cdread_normal(FILE *f, unsigned int base, void *dest, int sector);

// A CD image decoder uses this to determine if file it is asked to read
//  is different than the main (encoded) track, i.e. CD audio tracks.
FILE* GetCdFileHandle();
///////////////////////////////////////////////////////////////////////////////


extern unsigned int cdrIsoMultidiskCount;
extern unsigned int cdrIsoMultidiskSelect;

#endif
