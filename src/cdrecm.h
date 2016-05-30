////////////////////////////////////////////////////////////////////////////////
//
// cdrecm - Encoder/decoder for Error Code Modeler format
// Copyright (C) 2002-2011 Neill Corlett
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.
//
////////////////////////////////////////////////////////////////////////////////

#ifndef CDRECM_H
#define CDRECM_H

int handleecm(const char *isoname, FILE* cdh, s32* accurate_length);
int cdread_ecm_decode(FILE *f, unsigned int base, void *dest, int sector);

#endif //CDRECM_H
