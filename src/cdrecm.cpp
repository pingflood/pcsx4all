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

#include <stdint.h>
#include <climits>
#include "cdrom.h"
#include "cdriso.h"
#include "cdrecm.h"


//senquack - Adapted ECM support from PCSX Reloaded, which was in turn
// adapted from Neill Corlett's code (PCSX Rearmed lacked ECM support,
// on which our CD-ROM code is now based)
// TODO: Cleanup the original PCSX Reloaded code a bit perhaps?

////////////////////////////////////////////////////////////////////////////////
//
// Sector types
//
// Mode 1
// -----------------------------------------------------
//        0  1  2  3  4  5  6  7  8  9  A  B  C  D  E  F
// 0000h 00 FF FF FF FF FF FF FF FF FF FF 00 [-ADDR-] 01
// 0010h [---DATA...
// ...
// 0800h                                     ...DATA---]
// 0810h [---EDC---] 00 00 00 00 00 00 00 00 [---ECC...
// ...
// 0920h                                      ...ECC---]
// -----------------------------------------------------
//
// Mode 2 (XA), form 1
// -----------------------------------------------------
//        0  1  2  3  4  5  6  7  8  9  A  B  C  D  E  F
// 0000h 00 FF FF FF FF FF FF FF FF FF FF 00 [-ADDR-] 02
// 0010h [--FLAGS--] [--FLAGS--] [---DATA...
// ...
// 0810h             ...DATA---] [---EDC---] [---ECC...
// ...
// 0920h                                      ...ECC---]
// -----------------------------------------------------
//
// Mode 2 (XA), form 2
// -----------------------------------------------------
//        0  1  2  3  4  5  6  7  8  9  A  B  C  D  E  F
// 0000h 00 FF FF FF FF FF FF FF FF FF FF 00 [-ADDR-] 02
// 0010h [--FLAGS--] [--FLAGS--] [---DATA...
// ...
// 0920h                         ...DATA---] [---EDC---]
// -----------------------------------------------------
//
// ADDR:  Sector address, encoded as minutes:seconds:frames in BCD
// FLAGS: Used in Mode 2 (XA) sectors describing the type of sector; repeated
//        twice for redundancy
// DATA:  Area of the sector which contains the actual data itself
// EDC:   Error Detection Code
// ECC:   Error Correction Code
//

#define MAX(x, y) (((x) > (y)) ? (x) : (y))
#define ECM_HEADER_SIZE 4

static u32 len_decoded_ecm_buffer=0; // same as decoded ECM file length or 2x size
static u32 len_ecm_savetable=0; // same as sector count of decoded ECM file or 2x count

#ifdef ENABLE_ECM_FULL //setting this makes whole ECM to be decoded in-memory meaning buffer could eat up to 700 MB of memory
static void *decoded_ecm_buffer;
static u32 decoded_ecm_sectors=1; // initially sector 1 is always decoded
#else
static u32 decoded_ecm_sectors=0; // disabled
#endif

static boolean ecm_file_detected = FALSE;
static u32 prevsector;

static FILE* decoded_ecm = NULL;

// Function that is used to read CD normally when ECM read function
//  is asked to read a track that is not ECM decoded (CUE file with
//  only one track as ECM)
static int (*cdimg_read_func_normal)(FILE *f, unsigned int base, void *dest, int sector) = NULL;

typedef struct ECMFILELUT {
    s32 sector;
    s32 filepos;
} ECMFILELUT;

static ECMFILELUT* ecm_savetable = NULL;

static const size_t ECM_SECTOR_SIZE[4] = {
    1,
    2352,
    2336,
    2336
};

////////////////////////////////////////////////////////////////////////////////
//senquack - This was unused; disabled it
#if 0
static uint32_t get32lsb(const uint8_t* src) {
    return
        (((uint32_t)(src[0])) <<  0) |
        (((uint32_t)(src[1])) <<  8) |
        (((uint32_t)(src[2])) << 16) |
        (((uint32_t)(src[3])) << 24);
}
#endif

static void put32lsb(uint8_t* dest, uint32_t value) {
    dest[0] = (uint8_t)(value      );
    dest[1] = (uint8_t)(value >>  8);
    dest[2] = (uint8_t)(value >> 16);
    dest[3] = (uint8_t)(value >> 24);
}

////////////////////////////////////////////////////////////////////////////////
//
// LUTs used for computing ECC/EDC
//
static uint8_t  ecc_f_lut[256];
static uint8_t  ecc_b_lut[256];
static uint32_t edc_lut  [256];

static void eccedc_init(void) {
    size_t i;
    for(i = 0; i < 256; i++) {
        uint32_t edc = i;
        size_t j = (i << 1) ^ (i & 0x80 ? 0x11D : 0);
        ecc_f_lut[i] = j;
        ecc_b_lut[i ^ j] = i;
        for(j = 0; j < 8; j++) {
            edc = (edc >> 1) ^ (edc & 1 ? 0xD8018001 : 0);
        }
        edc_lut[i] = edc;
    }
}

////////////////////////////////////////////////////////////////////////////////
//
// Compute EDC for a block
//
static uint32_t edc_compute(
    uint32_t edc,
    const uint8_t* src,
    size_t size
) {
    for(; size; size--) {
        edc = (edc >> 8) ^ edc_lut[(edc ^ (*src++)) & 0xFF];
    }
    return edc;
}

//
// Write ECC block (either P or Q)
//
static void ecc_writepq(
    const uint8_t* address,
    const uint8_t* data,
    size_t major_count,
    size_t minor_count,
    size_t major_mult,
    size_t minor_inc,
    uint8_t* ecc
) {
    size_t size = major_count * minor_count;
    size_t major;
    for(major = 0; major < major_count; major++) {
        size_t index = (major >> 1) * major_mult + (major & 1);
        uint8_t ecc_a = 0;
        uint8_t ecc_b = 0;
        size_t minor;
        for(minor = 0; minor < minor_count; minor++) {
            uint8_t temp;
            if(index < 4) {
                temp = address[index];
            } else {
                temp = data[index - 4];
            }
            index += minor_inc;
            if(index >= size) { index -= size; }
            ecc_a ^= temp;
            ecc_b ^= temp;
            ecc_a = ecc_f_lut[ecc_a];
        }
        ecc_a = ecc_b_lut[ecc_f_lut[ecc_a] ^ ecc_b];
        ecc[major              ] = (ecc_a        );
        ecc[major + major_count] = (ecc_a ^ ecc_b);
    }
}

//
// Write ECC P and Q codes for a sector
//
static void ecc_writesector(
    const uint8_t *address,
    const uint8_t *data,
    uint8_t *ecc
) {
    ecc_writepq(address, data, 86, 24,  2, 86, ecc);        // P
    ecc_writepq(address, data, 52, 43, 86, 88, ecc + 0xAC); // Q
}

////////////////////////////////////////////////////////////////////////////////

static const uint8_t zeroaddress[4] = {0, 0, 0, 0};

////////////////////////////////////////////////////////////////////////////////
//
// Reconstruct a sector based on type
//
static void reconstruct_sector(
    uint8_t* sector, // must point to a full 2352-byte sector
    int8_t type
) {
    //
    // Sync
    //
    sector[0x000] = 0x00;
    sector[0x001] = 0xFF;
    sector[0x002] = 0xFF;
    sector[0x003] = 0xFF;
    sector[0x004] = 0xFF;
    sector[0x005] = 0xFF;
    sector[0x006] = 0xFF;
    sector[0x007] = 0xFF;
    sector[0x008] = 0xFF;
    sector[0x009] = 0xFF;
    sector[0x00A] = 0xFF;
    sector[0x00B] = 0x00;

    switch(type) {
    case 1:
        //
        // Mode
        //
        sector[0x00F] = 0x01;
        //
        // Reserved
        //
        sector[0x814] = 0x00;
        sector[0x815] = 0x00;
        sector[0x816] = 0x00;
        sector[0x817] = 0x00;
        sector[0x818] = 0x00;
        sector[0x819] = 0x00;
        sector[0x81A] = 0x00;
        sector[0x81B] = 0x00;
        break;
    case 2:
    case 3:
        //
        // Mode
        //
        sector[0x00F] = 0x02;
        //
        // Flags
        //
        sector[0x010] = sector[0x014];
        sector[0x011] = sector[0x015];
        sector[0x012] = sector[0x016];
        sector[0x013] = sector[0x017];
        break;
    }

    //
    // Compute EDC
    //
    switch(type) {
    case 1: put32lsb(sector+0x810, edc_compute(0, sector     , 0x810)); break;
    case 2: put32lsb(sector+0x818, edc_compute(0, sector+0x10, 0x808)); break;
    case 3: put32lsb(sector+0x92C, edc_compute(0, sector+0x10, 0x91C)); break;
    }

    //
    // Compute ECC
    //
    switch(type) {
    case 1: ecc_writesector(sector+0xC , sector+0x10, sector+0x81C); break;
    case 2: ecc_writesector(zeroaddress, sector+0x10, sector+0x81C); break;
    }

    //
    // Done
    //
}

/* Adapted from ecm.c:unecmify() (C) Neill Corlett */
int cdread_ecm_decode(FILE *f, unsigned int base, void *dest, int sector) {
	u32 b=0, writebytecount=0, num;
	s32 sectorcount=0;
	s8 type = 0; // mode type 0 (META) or 1, 2 or 3 for CDROM type
	u8 sector_buffer[CD_FRAMESIZE_RAW];
	boolean processsectors = (boolean)decoded_ecm_sectors; // this flag tells if to decode all sectors or just skip to wanted sector
	ECMFILELUT* pos = &(ecm_savetable[0]); // points always to beginning of ECM DATA

	//senquack - Disabled, as this var was only used by lines of code that
	// were commented out in PCSX Reloaded source code:
	//u32 output_edc=0;

	// If not pointing to ECM file but CDDA file or some other track
	if(f != GetCdFileHandle()) {
		//printf("BASETR %i %i\n", base, sector);
		return cdimg_read_func_normal(f, base, dest, sector);
	}
	// When sector exists in decoded ECM file buffer
	else if (decoded_ecm_sectors && sector < decoded_ecm_sectors) {
		//printf("ReadSector %i %i\n", sector, savedsectors);
		return cdimg_read_func_normal(decoded_ecm, base, dest, sector);
	}
	// To prevent invalid seek
	/* else if (sector > len_ecm_savetable) {
		printf("ECM: invalid sector requested\n");
		return -1;
	}*/
	//printf("SeekSector %i %i %i %i\n", sector, pos->sector, prevsector, base);

	if (sector <= len_ecm_savetable) {
		// get sector from LUT which points to wanted sector or close to
		// TODO: What would be optimal maximum to search near sector?
		//       Might cause slowdown if too small but too big also..
		for (sectorcount = sector; ((sectorcount > 0) && ((sector-sectorcount) <= 50000)); sectorcount--) {
			if (ecm_savetable[sectorcount].filepos >= ECM_HEADER_SIZE) {
				pos = &(ecm_savetable[sectorcount]);
				//printf("LUTSector %i %i %i %i\n", sector, pos->sector, prevsector, base);
				break;
			}
		}
		// if suitable sector was not found from LUT use last sector if less than wanted sector
		if (pos->filepos <= ECM_HEADER_SIZE && sector > prevsector) pos=&(ecm_savetable[prevsector]);
	}

	writebytecount = pos->sector * CD_FRAMESIZE_RAW;
	sectorcount = pos->sector;
	if (decoded_ecm_sectors) fseek(decoded_ecm, writebytecount, SEEK_SET); // rewind to last pos
	fseek(f, /*base+*/pos->filepos, SEEK_SET);
	while(sector >= sectorcount) { // decode ecm file until we are past wanted sector
		int c = fgetc(f);
		int bits = 5;
		if(c == EOF) { goto error_in; }
		type = c & 3;
		num = (c >> 2) & 0x1F;
		//printf("ECM1 file; count %x\n", c);
		while(c & 0x80) {
			c = fgetc(f);
			//printf("ECM2 file; count %x\n", c);
			if(c == EOF) { goto error_in; }
			if( (bits > 31) ||
					((uint32_t)(c & 0x7F)) >= (((uint32_t)0x80000000LU) >> (bits-1))
					) {
				printf("Corrupt ECM file; invalid sector count\n");
				goto error;
			}
			num |= ((uint32_t)(c & 0x7F)) << bits;
			bits += 7;
		}
		if(num == 0xFFFFFFFF) {
			// End indicator
			len_decoded_ecm_buffer = writebytecount;
			len_ecm_savetable = len_decoded_ecm_buffer/CD_FRAMESIZE_RAW;
			break;
		}
		num++;
		while(num) {
			if (!processsectors && sectorcount >= (sector-1)) { // ensure that we read the sector we are supposed to
				processsectors = TRUE;
				//printf("Saving at %i\n", sectorcount);
			} else if (processsectors && sectorcount > sector) {
				//printf("Terminating at %i\n", sectorcount);
				break;
			}
			/*printf("Type %i Num %i SeekSector %i ProcessedSectors %i(%i) Bytecount %i Pos %li Write %u\n",
					type, num, sector, sectorcount, pos->sector, writebytecount, ftell(f), processsectors);*/
			switch(type) {
			case 0: // META
				b = num;
				if(b > sizeof(sector_buffer)) { b = sizeof(sector_buffer); }
				writebytecount += b;
				if (!processsectors) { fseek(f, +b, SEEK_CUR); break; } // seek only
				if(fread(sector_buffer, 1, b, f) != b) {
					goto error_in;
				}
				//output_edc = edc_compute(output_edc, sector_buffer, b);
				if(decoded_ecm_sectors && fwrite(sector_buffer, 1, b, decoded_ecm) != b) { // just seek or write also
					goto error_out;
				}
				break;
			case 1: //Mode 1
				b=1;
				writebytecount += ECM_SECTOR_SIZE[type];
				if(fread(sector_buffer + 0x00C, 1, 0x003, f) != 0x003) { goto error_in; }
				if(fread(sector_buffer + 0x010, 1, 0x800, f) != 0x800) { goto error_in; }
				if (!processsectors) break; // seek only
				reconstruct_sector(sector_buffer, type);
				//output_edc = edc_compute(output_edc, sector_buffer, ECM_SECTOR_SIZE[type]);
				if(decoded_ecm_sectors && fwrite(sector_buffer, 1, ECM_SECTOR_SIZE[type], decoded_ecm) != ECM_SECTOR_SIZE[type]) { goto error_out; }
				break;
			case 2: //Mode 2 (XA), form 1
				b=1;
				writebytecount += ECM_SECTOR_SIZE[type];
				if (!processsectors) { fseek(f, +0x804, SEEK_CUR); break; } // seek only
				if(fread(sector_buffer + 0x014, 1, 0x804, f) != 0x804) { goto error_in; }
				reconstruct_sector(sector_buffer, type);
				//output_edc = edc_compute(output_edc, sector_buffer + 0x10, ECM_SECTOR_SIZE[type]);
				if(decoded_ecm_sectors && fwrite(sector_buffer + 0x10, 1, ECM_SECTOR_SIZE[type], decoded_ecm) != ECM_SECTOR_SIZE[type]) { goto error_out; }
				break;
			case 3: //Mode 2 (XA), form 2
				b=1;
				writebytecount += ECM_SECTOR_SIZE[type];
				if (!processsectors) { fseek(f, +0x918, SEEK_CUR); break; } // seek only
				if(fread(sector_buffer + 0x014, 1, 0x918, f) != 0x918) { goto error_in; }
				reconstruct_sector(sector_buffer, type);
				//output_edc = edc_compute(output_edc, sector_buffer + 0x10, ECM_SECTOR_SIZE[type]);
				if(decoded_ecm_sectors && fwrite(sector_buffer + 0x10, 1, ECM_SECTOR_SIZE[type], decoded_ecm) != ECM_SECTOR_SIZE[type]) { goto error_out; }
				break;
			}
			sectorcount=((writebytecount/CD_FRAMESIZE_RAW) - 0);
			num -= b;
		}
		if (type && sectorcount > 0 && ecm_savetable[sectorcount].filepos <= ECM_HEADER_SIZE ) {
			ecm_savetable[sectorcount].filepos = ftell(f)/*-base*/;
			ecm_savetable[sectorcount].sector = sectorcount;
			//printf("Marked %i at pos %i\n", ecm_savetable[sectorcount].sector, ecm_savetable[sectorcount].filepos);
		}
	}

	if (decoded_ecm_sectors) {
		fflush(decoded_ecm);
		fseek(decoded_ecm, -1*CD_FRAMESIZE_RAW, SEEK_CUR);
		num = fread(sector_buffer, 1, CD_FRAMESIZE_RAW, decoded_ecm);
		decoded_ecm_sectors = MAX(decoded_ecm_sectors, sectorcount);
	} else {
		num = CD_FRAMESIZE_RAW;
	}

	memcpy(dest, sector_buffer, CD_FRAMESIZE_RAW);
	prevsector = sectorcount;
	//printf("OK: Frame decoded %i %i\n", sectorcount-1, writebytecount);
	return num;

error_in:
error:
error_out:
	//memset(dest, 0x0, CD_FRAMESIZE_RAW);
	printf("Error decoding ECM image: WantedSector %i Type %i Base %i Sectors %i(%i) Pos %i(%li)\n",
				sector, type, base, sectorcount, pos->sector, writebytecount, ftell(f));
	return -1;
}

int handleecm(const char *isoname, FILE* cdh, s32* accurate_length) {
	// Rewind to start and check ECM header and filename suffix validity
	fseek(cdh, 0, SEEK_SET);
	if(
		(fgetc(cdh) == 'E') &&
		(fgetc(cdh) == 'C') &&
		(fgetc(cdh) == 'M') &&
		(fgetc(cdh) == 0x00) &&
		(strncmp((isoname+strlen(isoname)-5), ".ecm", 4))
	) {
		// Function used to read CD normally
		// TODO: detect if 2048 and use it
		// senquack TODO: make code more robust/cleaner in this regard,
		//  perhaps making it so cdriso.cpp never calls this function for
		//  non-ECM tracks like multi-bin CUEs with non-ECM audio files.
		//  Also: add support for more than one ECM track in a CUE file
		//  (not just the 1st track which is FILE *cdHandle, basically
		//  allowing separate ECM-encoded audio tracks.
		cdimg_read_func_normal = cdread_normal;

		// Last accessed sector
		prevsector = 0;

		// Already analyzed during this session, use cached results
		if (ecm_file_detected) {
			if (accurate_length) *accurate_length = len_ecm_savetable;
			return 0;
		}

		printf("\nDetected ECM file with proper header and filename suffix.\n");

		// Init ECC/EDC tables
		eccedc_init();

		// Reserve maximum known sector ammount for LUT (80MIN CD)
		len_ecm_savetable = 75*80*60; //2*(accurate_length/CD_FRAMESIZE_RAW);

		// Index 0 always points to beginning of ECM data
		ecm_savetable = (ECMFILELUT *)calloc(len_ecm_savetable, sizeof(ECMFILELUT)); // calloc returns nulled data
		ecm_savetable[0].filepos = ECM_HEADER_SIZE;

		if (accurate_length || decoded_ecm_sectors) {
			u8 tbuf1[CD_FRAMESIZE_RAW];
			len_ecm_savetable = 0; // indicates to cdread_ecm_decode that no lut has been built yet
			cdread_ecm_decode(cdh, 0U, tbuf1, INT_MAX); // builds LUT completely
			if (accurate_length)*accurate_length = len_ecm_savetable;
		}

		// Full image decoded? Needs fmemopen()
#ifdef ENABLE_ECM_FULL //setting this makes whole ECM to be decoded in-memory meaning buffer could eat up to 700 MB of memory
		if (decoded_ecm_sectors) {
			len_decoded_ecm_buffer = len_ecm_savetable*CD_FRAMESIZE_RAW;
			decoded_ecm_buffer = malloc(len_decoded_ecm_buffer);
			if (decoded_ecm_buffer) {
				//printf("Memory ok1 %u %p\n", len_decoded_ecm_buffer, decoded_ecm_buffer);
				decoded_ecm = fmemopen(decoded_ecm_buffer, len_decoded_ecm_buffer, "w+b");
				decoded_ecm_sectors = 1;
			} else {
				printf("Could not reserve memory for full ECM buffer. Only LUT will be used.\n");
				decoded_ecm_sectors = 0;
			}
		}
#endif

		ecm_file_detected = TRUE;

		return 0;
	}
	return -1;
}
