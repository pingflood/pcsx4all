#include <dirent.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "port.h"
#include "font.h"
#include "r3000a.h"
#include "plugins.h"
#include "plugin_lib.h"
#include "perfmon.h"
#include <SDL.h>
#include <SDL_image.h>

/* PATH_MAX inclusion */
#ifdef __MINGW32__
#include <limits.h>
#endif

#ifdef SPU_PCSXREARMED
#include "spu/spu_pcsxrearmed/spu_config.h"		// To set spu-specific configuration
#endif

// New gpulib from Notaz's PCSX Rearmed handles duties common to GPU plugins
#ifdef USE_GPULIB
#include "gpu/gpulib/gpu.h"
#endif

#ifdef GPU_UNAI
#include "gpu/gpu_unai/gpu.h"
#endif

enum {
	DKEY_SELECT = 0,
	DKEY_L3,
	DKEY_R3,
	DKEY_START,
	DKEY_UP,
	DKEY_RIGHT,
	DKEY_DOWN,
	DKEY_LEFT,
	DKEY_L2,
	DKEY_R2,
	DKEY_L1,
	DKEY_R1,
	DKEY_TRIANGLE,
	DKEY_CIRCLE,
	DKEY_CROSS,
	DKEY_SQUARE,

	DKEY_TOTAL
};

// static SDL_Surface *screen;
SDL_Surface *screen;
unsigned short *SCREEN;

static bool pcsx4all_initted = false;
static bool emu_running = false;

void config_load();
void config_save();

static void pcsx4all_exit(void)
{
	if (SDL_MUSTLOCK(screen))
		SDL_UnlockSurface(screen);

	SDL_Quit();

	if (pcsx4all_initted == true) {
		ReleasePlugins();
		psxShutdown();
	}

	// Store config to file
	config_save();
}

static char *home = NULL;
static char homedir[PATH_MAX] =		"./.pcsx4all";
static char memcardsdir[PATH_MAX] =	"./.pcsx4all/memcards";
static char biosdir[PATH_MAX] =		"./.pcsx4all/bios";
static char patchesdir[PATH_MAX] =	"./.pcsx4all/patches";
char sstatesdir[PATH_MAX] = "./.pcsx4all/sstates";

static char McdPath1[PATH_MAX] = "";
static char McdPath2[PATH_MAX] = "";
static char BiosFile[PATH_MAX] = "";

#ifdef __WIN32__
	#define MKDIR(A) mkdir(A)
#else
	#define MKDIR(A) mkdir(A, 0777)
#endif

static void setup_paths()
{
#ifndef __WIN32__
	home = getenv("HOME");
#else
	char buf[PATH_MAX];
	home = getcwd(buf, PATH_MAX);
#endif
	if (home) {
		sprintf(homedir, "%s/.pcsx4all", home);
		sprintf(sstatesdir, "%s/sstates", homedir);
		sprintf(memcardsdir, "%s/memcards", homedir);
		sprintf(biosdir, "%s/bios", homedir);
		sprintf(patchesdir, "%s/patches", homedir);
	}

	MKDIR(homedir);
	MKDIR(sstatesdir);
	MKDIR(memcardsdir);
	MKDIR(biosdir);
	MKDIR(patchesdir);
}

void probe_lastdir()
{
	DIR *dir;
	if (!Config.LastDir)
		return;

	dir = opendir(Config.LastDir);

	if (!dir) {
		// Fallback to home directory.
		strncpy(Config.LastDir, home, MAXPATHLEN);
		Config.LastDir[MAXPATHLEN-1] = '\0';
	} else {
		closedir(dir);
	}
}

#ifdef PSXREC
extern u32 cycle_multiplier; // in mips/recompiler.cpp
#endif

void config_load()
{
	FILE *f;
	char *config = (char *)malloc(strlen(homedir) + strlen("/pcsx4all.retrofw.cfg") + 1);
	char line[strlen("LastDir ") + MAXPATHLEN + 1];
	int lineNum = 0;

	if (!config)
		return;

	sprintf(config, "%s/pcsx4all.retrofw.cfg", homedir);

	f = fopen(config, "r");

	if (f == NULL) {
		printf("Failed to open config file: \"%s\" for reading.\n", config);
		free(config);
		return;
	}

	while (fgets(line, sizeof(line), f)) {
		char *arg = strchr(line, ' ');
		int value;

		++lineNum;

		if (!arg) {
			continue;
		}

		*arg = '\0';
		arg++;

		if (lineNum == 1) {
			if (!strcmp(line, "CONFIG_VERSION")) {
				sscanf(arg, "%d", &value);
				if (value == CONFIG_VERSION) {
					continue;
				} else {
					printf("Incompatible config version for \"%s\"."
					       "Required: %d. Found: %d. Ignoring.\n",
					       config, CONFIG_VERSION, value);
					break;
				}
			}

			printf("Incompatible config format for \"%s\"."
			       "Ignoring.\n", config);
			break;
		}

		if (!strcmp(line, "Xa")) {
			sscanf(arg, "%d", &value);
			Config.Xa = value;
		} else if (!strcmp(line, "Mdec")) {
			sscanf(arg, "%d", &value);
			Config.Mdec = value;
		} else if (!strcmp(line, "PsxAuto")) {
			sscanf(arg, "%d", &value);
			Config.PsxAuto = value;
		} else if (!strcmp(line, "Cdda")) {
			sscanf(arg, "%d", &value);
			Config.Cdda = value;
		} else if (!strcmp(line, "HLE")) {
			sscanf(arg, "%d", &value);
			Config.HLE = value;
		} else if (!strcmp(line, "SlowBoot")) {
			sscanf(arg, "%d", &value);
			Config.SlowBoot = value;
		} else if (!strcmp(line, "RCntFix")) {
			sscanf(arg, "%d", &value);
			Config.RCntFix = value;
		} else if (!strcmp(line, "VSyncWA")) {
			sscanf(arg, "%d", &value);
			Config.VSyncWA = value;
		} else if (!strcmp(line, "Cpu")) {
			sscanf(arg, "%d", &value);
			Config.Cpu = value;
		} else if (!strcmp(line, "PsxType")) {
			sscanf(arg, "%d", &value);
			Config.PsxType = value;
		} else if (!strcmp(line, "McdSlot1")) {
            sscanf(arg, "%d", &value);
            Config.McdSlot1 = value;
        } else if (!strcmp(line, "McdSlot2")) {
            sscanf(arg, "%d", &value);
            Config.McdSlot2 = value;
		} else if (!strcmp(line, "SpuIrq")) {
			sscanf(arg, "%d", &value);
			Config.SpuIrq = value;
		} else if (!strcmp(line, "SyncAudio")) {
			sscanf(arg, "%d", &value);
			Config.SyncAudio = value;
		} else if (!strcmp(line, "SpuUpdateFreq")) {
			sscanf(arg, "%d", &value);
			if (value < SPU_UPDATE_FREQ_MIN || value > SPU_UPDATE_FREQ_MAX)
				value = SPU_UPDATE_FREQ_DEFAULT;
			Config.SpuUpdateFreq = value;
		} else if (!strcmp(line, "ForcedXAUpdates")) {
			sscanf(arg, "%d", &value);
			if (value < FORCED_XA_UPDATES_MIN || value > FORCED_XA_UPDATES_MAX)
				value = FORCED_XA_UPDATES_DEFAULT;
			Config.ForcedXAUpdates = value;
		} else if (!strcmp(line, "ShowFps")) {
			sscanf(arg, "%d", &value);
			Config.ShowFps = value;
		} else if (!strcmp(line, "FrameLimit")) {
			sscanf(arg, "%d", &value);
			Config.FrameLimit = value;
		} else if (!strcmp(line, "FrameSkip")) {
			sscanf(arg, "%d", &value);
			if (value < FRAMESKIP_MIN || value > FRAMESKIP_MAX)
				value = FRAMESKIP_OFF;
			Config.FrameSkip = value;
		}
#ifdef SPU_PCSXREARMED
		else if (!strcmp(line, "SpuUseInterpolation")) {
			sscanf(arg, "%d", &value);
			spu_config.iUseInterpolation = value;
		} else if (!strcmp(line, "SpuUseReverb")) {
			sscanf(arg, "%d", &value);
			spu_config.iUseReverb = value;
		} else if (!strcmp(line, "SpuVolume")) {
			sscanf(arg, "%d", &value);
			if (value > 1024) value = 1024;
			if (value < 0) value = 0;
			spu_config.iVolume = value;
		}
#endif
		else if (!strcmp(line, "LastDir")) {
			int len = strlen(arg);

			if (len == 0 || len > sizeof(Config.LastDir) - 1) {
				continue;
			}

			if (arg[len-1] == '\n') {
				arg[len-1] = '\0';
			}

			strcpy(Config.LastDir, arg);
		} else if (!strcmp(line, "BiosDir")) {
			int len = strlen(arg);

			if (len == 0 || len > sizeof(Config.BiosDir) - 1) {
				continue;
			}

			if (arg[len-1] == '\n') {
				arg[len-1] = '\0';
			}

			strcpy(Config.BiosDir, arg);
		} else if (!strcmp(line, "Bios")) {
			int len = strlen(arg);

			if (len == 0 || len > sizeof(Config.Bios) - 1) {
				continue;
			}

			if (arg[len-1] == '\n') {
				arg[len-1] = '\0';
			}

			strcpy(Config.Bios, arg);
		}
#ifdef PSXREC
		else if (!strcmp(line, "CycleMultiplier")) {
			sscanf(arg, "%03x", &value);
			cycle_multiplier = value;
		}
#endif
#ifdef GPU_UNAI
		else if (!strcmp(line, "pixel_skip")) {
			sscanf(arg, "%d", &value);
			gpu_unai_config_ext.pixel_skip = value;
		} else if (!strcmp(line, "lighting")) {
			sscanf(arg, "%d", &value);
			gpu_unai_config_ext.lighting = value;
		} else if (!strcmp(line, "fast_lighting")) {
			sscanf(arg, "%d", &value);
			gpu_unai_config_ext.fast_lighting = value;
		} else if (!strcmp(line, "blending")) {
			sscanf(arg, "%d", &value);
			gpu_unai_config_ext.blending = value;
		} else if (!strcmp(line, "dithering")) {
			sscanf(arg, "%d", &value);
			gpu_unai_config_ext.dithering = value;
		} else if (!strcmp(line, "interlace")) {
			sscanf(arg, "%d", &value);
			gpu_unai_config_ext.ilace_force = value;
		}
#endif
	}

	fclose(f);
	free(config);
}

void config_save()
{
	FILE *f;
	char *config = (char *)malloc(strlen(homedir) + strlen("/pcsx4all.retrofw.cfg") + 1);

	if (!config)
		return;

	sprintf(config, "%s/pcsx4all.retrofw.cfg", homedir);

	f = fopen(config, "w");

	if (f == NULL) {
		printf("Failed to open config file: \"%s\" for writing.\n", config);
		free(config);
		return;
	}

	fprintf(f, "CONFIG_VERSION %d\n"
		   "Xa %d\n"
		   "Mdec %d\n"
		   "PsxAuto %d\n"
		   "Cdda %d\n"
		   "HLE %d\n"
		   "SlowBoot %d\n"
		   "RCntFix %d\n"
		   "VSyncWA %d\n"
		   "Cpu %d\n"
		   "PsxType %d\n"
		   "McdSlot1 %d\n"
		   "McdSlot2 %d\n"
		   "SpuIrq %d\n"
		   "SyncAudio %d\n"
		   "SpuUpdateFreq %d\n"
		   "ForcedXAUpdates %d\n"
		   "ShowFps %d\n"
		   "FrameLimit %d\n"
		   "FrameSkip %d\n",
		   CONFIG_VERSION, Config.Xa, Config.Mdec, Config.PsxAuto,
		   Config.Cdda, Config.HLE, Config.SlowBoot, Config.RCntFix, Config.VSyncWA,
		   Config.Cpu, Config.PsxType, Config.McdSlot1, Config.McdSlot2, Config.SpuIrq, Config.SyncAudio,
		   Config.SpuUpdateFreq, Config.ForcedXAUpdates, Config.ShowFps, Config.FrameLimit,
		   Config.FrameSkip);

#ifdef SPU_PCSXREARMED
	fprintf(f, "SpuUseInterpolation %d\n", spu_config.iUseInterpolation);
	fprintf(f, "SpuUseReverb %d\n", spu_config.iUseReverb);
	fprintf(f, "SpuVolume %d\n", spu_config.iVolume);
#endif

#ifdef PSXREC
	fprintf(f, "CycleMultiplier %03x\n", cycle_multiplier);
#endif

#ifdef GPU_UNAI
	fprintf(f, "interlace %d\n"
		   "pixel_skip %d\n"
		   "lighting %d\n"
		   "fast_lighting %d\n"
		   "blending %d\n"
		   "dithering %d\n",
		   gpu_unai_config_ext.ilace_force,
		   gpu_unai_config_ext.pixel_skip,
		   gpu_unai_config_ext.lighting,
		   gpu_unai_config_ext.fast_lighting,
		   gpu_unai_config_ext.blending,
		   gpu_unai_config_ext.dithering);
#endif


	if (Config.LastDir[0]) {
		fprintf(f, "LastDir %s\n", Config.LastDir);
	}

	if (Config.BiosDir[0]) {
		fprintf(f, "BiosDir %s\n", Config.BiosDir);
	}

	if (Config.Bios[0]) {
		fprintf(f, "Bios %s\n", Config.Bios);
	}

	fclose(f);
	free(config);
}

// Returns 0: success, -1: failure
int state_load(int slot)
{
	char savename[512];
	sprintf(savename, "%s/%s.%d.sav", sstatesdir, CdromId, slot);

	if (FileExists(savename)) {
		return LoadState(savename);
	}

	return -1;
}

// Returns 0: success, -1: failure
int state_save(int slot)
{
	char savename[512];
	sprintf(savename, "%s/%s.%d.sav", sstatesdir, CdromId, slot);

	return SaveState(savename);
}

static struct {
	int key;
	int bit;
} keymap[] = {
	{ SDLK_UP,		DKEY_UP },
	{ SDLK_DOWN,		DKEY_DOWN },
	{ SDLK_LEFT,		DKEY_LEFT },
	{ SDLK_RIGHT,		DKEY_RIGHT },
#ifdef GCW_ZERO
	{ SDLK_SPACE,		DKEY_SQUARE },
	{ SDLK_LALT,		DKEY_CIRCLE },
	{ SDLK_LSHIFT,		DKEY_TRIANGLE },
	{ SDLK_LCTRL,		DKEY_CROSS },
	{ SDLK_TAB,		DKEY_L1 },
	{ SDLK_BACKSPACE,	DKEY_R1 },
	{ SDLK_ESCAPE,		DKEY_SELECT },
	{ SDLK_1,		DKEY_L2 },
	{ SDLK_2,	        DKEY_R2 },
#else
	{ SDLK_a,		DKEY_SQUARE },
	{ SDLK_x,		DKEY_CIRCLE },
	{ SDLK_s,		DKEY_TRIANGLE },
	{ SDLK_z,		DKEY_CROSS },
	{ SDLK_q,		DKEY_L1 },
	{ SDLK_w,		DKEY_R1 },
	{ SDLK_e,		DKEY_L2 },
	{ SDLK_r,		DKEY_R2 },
	{ SDLK_BACKSPACE,	DKEY_SELECT },
#endif
	{ SDLK_RETURN,		DKEY_START },
	{ 0, 0 }
};

static unsigned short pad1 = 0xffff;
static unsigned short pad2 = 0xffff;

void pad_update(void)
{
	SDL_Event event;
	Uint8 *keys = SDL_GetKeyState(NULL);

	while (SDL_PollEvent(&event)) {
		switch (event.type) {
		case SDL_QUIT:
			exit(0);
			break;
		case SDL_KEYDOWN:
			switch (event.key.keysym.sym) {
#ifndef GCW_ZERO
			case SDLK_ESCAPE:
				event.type = SDL_QUIT;
				SDL_PushEvent(&event);
				break;
#endif
			case SDLK_v: { Config.ShowFps=!Config.ShowFps; } break;
			default: break;
			}
			break;

		default: break;
		}
	}

	int k = 0;
	while (keymap[k].key) {
		if (keys[keymap[k].key]) {
			pad1 &= ~(1 << keymap[k].bit);
		} else {
			pad1 |= (1 << keymap[k].bit);
		}
		k++;
	}

	/* Special key combos for GCW-Zero */
#ifdef GCW_ZERO
// SELECT+L1 for psx's L2
	if (keys[SDLK_ESCAPE] && keys[SDLK_TAB]) {
		pad1 &= ~(1 << DKEY_L2);
		pad1 |= (1 << DKEY_L1);
	} else if (keys[SDLK_1]) {
		pad1 &= ~(1 << DKEY_L2);
	} else {
		pad1 |= (1 << DKEY_L2);
	}

	// SELECT+R1 for R2
	if (keys[SDLK_ESCAPE] && keys[SDLK_BACKSPACE]) {
		pad1 &= ~(1 << DKEY_R2);
		pad1 |= (1 << DKEY_R1);
	} else if (keys[SDLK_2]) {
		pad1 &= ~(1 << DKEY_R2);
	} else {
		pad1 |= (1 << DKEY_R2);
	}

	// SELECT+START for menu
	if ((keys[SDLK_ESCAPE] && keys[SDLK_RETURN] && !keys[SDLK_LALT])|| keys[SDLK_END]) {
		//Sync and close any memcard files opened for writing
		//TODO: Disallow entering menu until they are synced/closed
		// automatically, displaying message that write is in progress.
		sioSyncMcds();

		emu_running = false;
		pl_pause();    // Tell plugin_lib we're pausing emu
		GameMenu();
		emu_running = true;
		pad1 |= (1 << DKEY_START);
		pad1 |= (1 << DKEY_CIRCLE);
		video_clear();
		video_flip();
		video_clear();
#ifdef SDL_TRIPLEBUF
		video_flip();
		video_clear();
#endif
		pl_resume();    // Tell plugin_lib we're reentering emu
	}
#endif
}

unsigned short pad_read(int num)
{
	return (num == 0 ? pad1 : pad2);
}

void video_blit(void *src)
{
	if (SDL_MUSTLOCK(screen)) SDL_UnlockSurface(screen);
	SDL_BlitSurface((SDL_Surface*)src, NULL, screen, NULL);
	if (SDL_MUSTLOCK(screen)) SDL_LockSurface(screen);
}

void video_flip(void)
{
	if (emu_running && Config.ShowFps) {
		port_printf_fg_bg(5, 5, pl_data.stats_msg, 0xffff, 0x0000);
	}

	if (SDL_MUSTLOCK(screen))
		SDL_UnlockSurface(screen);

	SDL_Flip(screen);

	if (SDL_MUSTLOCK(screen))
		SDL_LockSurface(screen);

	SCREEN = (Uint16 *)screen->pixels;
}

/* This is used by gpu_dfxvideo only as it doesn't scale itself */
#ifdef GPU_DFXVIDEO
void video_set(unsigned short *pVideo, unsigned int width, unsigned int height)
{
	int y;
	unsigned short *ptr = SCREEN;
	int w = (width > 320 ? 320 : width);
	int h = (height > 240 ? 240 : height);

	for (y = 0; y < h; y++) {
		memcpy(ptr, pVideo, w * 2);
		ptr += 320;
		pVideo += width;
	}

	video_flip();
}
#endif

void video_clear(void)
{
	memset(screen->pixels, 0, screen->pitch*screen->h);
}

const char *GetMemcardPath(int slot) {
	switch(slot) {
	case 1:
		return McdPath1;
	case 2:
		return McdPath2;
	}
	return NULL;
}

void update_memcards(int load_mcd) {
	sprintf(McdPath1, "%s/mcd%03d.mcr", memcardsdir, (int) Config.McdSlot1);
	sprintf(McdPath2, "%s/mcd%03d.mcr", memcardsdir, (int) Config.McdSlot2);
	if (load_mcd & 1) {
		printf("Loading memcard: %s\n", McdPath1);
		LoadMcd(MCD1, McdPath1); //Memcard 1
	}
	if (load_mcd & 2) {
		printf("Loading memcard: %s\n", McdPath2);
		LoadMcd(MCD2, McdPath2); //Memcard 2
	}
}

const char *bios_file_get() {
	if (BiosFile[0])
		return BiosFile;
	else
		return "HLE";
}

void bios_file_set(const char *filename) {
	strcpy(Config.Bios, filename);
	strcpy(BiosFile, filename);
}

// if [CdromId].bin is exsit, use the spec bios
void check_spec_bios() {
	FILE *f = NULL;
	char bios[MAXPATHLEN];
	sprintf(bios, "%s/%s.bin", Config.BiosDir, CdromId);
	f = fopen(bios, "rb");
	if (f == NULL) {
		strcpy(BiosFile, Config.Bios);
		return;
	}
	fclose(f);
	sprintf(BiosFile, "%s.bin", CdromId);
}

/* This is needed to override redirecting to stderr.txt and stdout.txt
with mingw build. */
#ifdef UNDEF_MAIN
#undef main
#endif

int main (int argc, char **argv)
{
	char filename[256];
	const char *cdrfilename = GetIsoFile();

	filename[0] = '\0'; /* Executable file name */

	setup_paths();

	// PCSX
	Config.McdSlot1 = 1;
	Config.McdSlot2 = 2;
	update_memcards(0);

	strcpy(Config.PatchesDir, patchesdir);
	strcpy(Config.BiosDir, biosdir);
	strcpy(Config.Bios, "");

	Config.Xa=0; /* 0=XA enabled, 1=XA disabled */
	Config.Mdec=0; /* 0=Black&White Mdecs Only Disabled, 1=Black&White Mdecs Only Enabled */
	Config.PsxAuto=1; /* 1=autodetect system (pal or ntsc) */
	Config.PsxType=0; /* PSX_TYPE_NTSC=ntsc, PSX_TYPE_PAL=pal */
	Config.Cdda=0; /* 0=Enable Cd audio, 1=Disable Cd audio */
	Config.HLE=1; /* 0=BIOS, 1=HLE */
#if defined (PSXREC)
	Config.Cpu=0; /* 0=recompiler, 1=interpreter */
#else
	Config.Cpu=1; /* 0=recompiler, 1=interpreter */
#endif
	Config.SlowBoot=0; /* 0=skip bios logo sequence on boot  1=show sequence (does not apply to HLE) */
	Config.RCntFix=0; /* 1=Parasite Eve 2, Vandal Hearts 1/2 Fix */
	Config.VSyncWA=0; /* 1=InuYasha Sengoku Battle Fix */
	Config.SpuIrq=0; /* 1=SPU IRQ always on, fixes some games */

	Config.SyncAudio=0;	/* 1=emu waits if audio output buffer is full
	                       (happens seldom with new auto frame limit) */

	// Number of times per frame to update SPU. Rearmed default is once per
	//  frame, but we are more flexible (for slower devices).
	//  Valid values: SPU_UPDATE_FREQ_1 .. SPU_UPDATE_FREQ_32
	Config.SpuUpdateFreq = SPU_UPDATE_FREQ_DEFAULT;

	//senquack - Added option to allow queuing CDREAD_INT interrupts sooner
	//           than they'd normally be issued when SPU's XA buffer is not
	//           full. This fixes droupouts in music/speech on slow devices.
	Config.ForcedXAUpdates = FORCED_XA_UPDATES_DEFAULT;

	Config.ShowFps=0;    // 0=don't show FPS
	Config.FrameLimit = true;
	Config.FrameSkip = FRAMESKIP_OFF;

	//zear - Added option to store the last visited directory.
	strncpy(Config.LastDir, home, MAXPATHLEN); /* Defaults to home directory. */
	Config.LastDir[MAXPATHLEN-1] = '\0';

	// senquack - added spu_pcsxrearmed plugin:
#ifdef SPU_PCSXREARMED
	//ORIGINAL PCSX ReARMed SPU defaults (put here for reference):
	//	spu_config.iUseReverb = 1;
	//	spu_config.iUseInterpolation = 1;
	//	spu_config.iXAPitch = 0;
	//	spu_config.iVolume = 768;
	//	spu_config.iTempo = 0;
	//	spu_config.iUseThread = 1; // no effect if only 1 core is detected
	//	// LOW-END DEVICE:
	//	#ifdef HAVE_PRE_ARMV7 /* XXX GPH hack */
	//		spu_config.iUseReverb = 0;
	//		spu_config.iUseInterpolation = 0;
	//		spu_config.iTempo = 1;
	//	#endif

	// PCSX4ALL defaults:
	// NOTE: iUseThread *will* have an effect even on a single-core device, but
	//		 results have yet to be tested. TODO: test if using iUseThread can
	//		 improve sound dropouts in any cases.
	spu_config.iHaveConfiguration = 1;    // *MUST* be set to 1 before calling SPU_Init()
	spu_config.iUseReverb = 0;
	spu_config.iUseInterpolation = 0;
	spu_config.iXAPitch = 0;
	spu_config.iVolume = 1024;            // 1024 is max volume
	spu_config.iUseThread = 0;            // no effect if only 1 core is detected
	spu_config.iUseFixedUpdates = 1;      // This is always set to 1 in libretro's pcsxReARMed
	spu_config.iTempo = 1;                // see note below
#endif

	//senquack - NOTE REGARDING iTempo config var above
	// From thread https://pyra-handheld.com/boards/threads/pcsx-rearmed-r22-now-using-the-dsp.75388/
	// Notaz says that setting iTempo=1 restores pcsxreARMed SPU's old behavior, which allows slow emulation
	// to not introduce audio dropouts (at least I *think* he's referring to iTempo config setting)
	// "Probably the main change is SPU emulation, there were issues in some games where effects were wrong,
	//  mostly Final Fantasy series, it should be better now. There were also sound sync issues where game would
	//  occasionally lock up (like Valkyrie Profile), it should be stable now.
	//  Changed sync has a side effect however - if the emulator is not fast enough (may happen with double
	//  resolution mode or while underclocking), sound will stutter more instead of slowing down the music itself.
	//  There is a new option in SPU plugin config to restore old inaccurate behavior if anyone wants it." -Notaz

	// gpu_dfxvideo
#ifdef GPU_DFXVIDEO
	extern int UseFrameLimit; UseFrameLimit=0; // limit fps 1=on, 0=off
	extern int UseFrameSkip; UseFrameSkip=0; // frame skip 1=on, 0=off
	extern int iFrameLimit; iFrameLimit=0; // fps limit 2=auto 1=fFrameRate, 0=off
	//senquack - TODO: is this really wise to have set to 200 as default:
	extern float fFrameRate; fFrameRate=200.0f; // fps
	extern int iUseDither; iUseDither=0; // 0=off, 1=game dependant, 2=always
	extern int iUseFixes; iUseFixes=0; // use game fixes
	extern uint32_t dwCfgFixes; dwCfgFixes=0; // game fixes
	/*
	 1=odd/even hack (Chrono Cross)
	 2=expand screen width (Capcom fighting games)
	 4=ignore brightness color (black screens in Lunar)
	 8=disable coordinate check (compatibility mode)
	 16=disable cpu saving (for precise framerate)
	 32=PC fps calculation (better fps limit in some games)
	 64=lazy screen update (Pandemonium 2)
	 128=old frame skipping (skip every second frame)
	 256=repeated flat tex triangles (Dark Forces)
	 512=draw quads with triangles (better g-colors, worse textures)
	*/
#endif //GPU_DFXVIDEO

	// gpu_drhell
#ifdef GPU_DRHELL
	extern unsigned int autoFrameSkip; autoFrameSkip=1; /* auto frameskip */
	extern signed int framesToSkip; framesToSkip=0; /* frames to skip */
#endif //GPU_DRHELL

	// gpu_unai
#ifdef GPU_UNAI
	gpu_unai_config_ext.ilace_force = 0;
	gpu_unai_config_ext.pixel_skip = 1;
	gpu_unai_config_ext.lighting = 1;
	gpu_unai_config_ext.fast_lighting = 1;
	gpu_unai_config_ext.blending = 1;
	gpu_unai_config_ext.dithering = 0;
#endif

	// Load config from file.
	config_load();

	// Check if LastDir exists.
	probe_lastdir();

	// command line options
	bool param_parse_error = 0;
	for (int i = 1; i < argc; i++) {
		// PCSX
		// XA audio disabled
		if (strcmp(argv[i],"-noxa") == 0)
			Config.Xa = 1;

		// Black & White MDEC
		if (strcmp(argv[i],"-bwmdec") == 0)
			Config.Mdec = 1;

		// Force PAL system
		if (strcmp(argv[i],"-pal") == 0) {
			Config.PsxAuto = 0;
			Config.PsxType = 1;
		}

		// Force NTSC system
		if (strcmp(argv[i],"-ntsc") == 0) {
			Config.PsxAuto = 0;
			Config.PsxType = 0;
		}

		// CD audio disabled
		if (strcmp(argv[i],"-nocdda") == 0)
			Config.Cdda = 1;

		// BIOS enabled
		if (strcmp(argv[i],"-bios") == 0)
			Config.HLE = 0;

		// Interpreter enabled
		if (strcmp(argv[i],"-interpreter") == 0)
			Config.Cpu = 1;

		// Show BIOS logo sequence at BIOS startup (doesn't apply to HLE)
		if (strcmp(argv[i],"-slowboot") == 0)
			Config.SlowBoot = 1;

		// Parasite Eve 2, Vandal Hearts 1/2 Fix
		if (strcmp(argv[i],"-rcntfix") == 0)
			Config.RCntFix = 1;

		// InuYasha Sengoku Battle Fix
		if (strcmp(argv[i],"-vsyncwa") == 0)
			Config.VSyncWA = 1;

		// SPU IRQ always enabled (fixes audio in some games)
		if (strcmp(argv[i],"-spuirq") == 0)
			Config.SpuIrq = 1;

		// Set ISO file
		if (strcmp(argv[i],"-iso") == 0)
			SetIsoFile(argv[i + 1]);

		// Set executable file
		if (strcmp(argv[i],"-file") == 0)
			strcpy(filename, argv[i + 1]);

		// Audio synchronization option: if audio buffer full, main thread
		//  blocks. Otherwise, just drop the samples.
		if (strcmp(argv[i],"-syncaudio") == 0)
			Config.SyncAudio = 0;

		// Number of times per frame to update SPU. PCSX Rearmed default is once
		//  per frame, but we are more flexible. Valid value is 0..5, where
		//  0 is once per frame, 5 is 32 times per frame (2^5)
		if (strcmp(argv[i],"-spuupdatefreq") == 0) {
			int val = -1;
			if (++i < argc) {
				val = atoi(argv[i]);
				if (val >= SPU_UPDATE_FREQ_MIN && val <= SPU_UPDATE_FREQ_MAX) {
					Config.SpuUpdateFreq = val;
				} else val = -1;
			} else {
				printf("ERROR: missing value for -spuupdatefreq\n");
			}

			if (val == -1) {
				printf("ERROR: -spuupdatefreq value must be between %d..%d\n"
				       "(%d is once per frame)\n",
					   SPU_UPDATE_FREQ_MIN, SPU_UPDATE_FREQ_MAX, SPU_UPDATE_FREQ_1);
				param_parse_error = true;
				break;
			}
		}

		//senquack - Added option to allow queuing CDREAD_INT interrupts sooner
		//           than they'd normally be issued when SPU's XA buffer is not
		//           full. This fixes droupouts in music/speech on slow devices.
		if (strcmp(argv[i],"-forcedxaupdates") == 0) {
			int val = -1;
			if (++i < argc) {
				val = atoi(argv[i]);
				if (val >= FORCED_XA_UPDATES_MIN && val <= FORCED_XA_UPDATES_MAX) {
					Config.ForcedXAUpdates = val;
				} else val = -1;
			} else {
				printf("ERROR: missing value for -forcedxaupdates\n");
			}

			if (val == -1) {
				printf("ERROR: -forcedxaupdates value must be between %d..%d\n",
					   FORCED_XA_UPDATES_MIN, FORCED_XA_UPDATES_MAX);
				param_parse_error = true;
				break;
			}
		}

		// Performance monitoring options
		if (strcmp(argv[i],"-perfmon") == 0) {
			// Enable detailed stats and console output
			Config.PerfmonConsoleOutput = true;
			Config.PerfmonDetailedStats = true;
		}

		// GPU
		// show FPS
		if (strcmp(argv[i],"-showfps") == 0) {
			Config.ShowFps = true;
		}

		// frame limit
		if (strcmp(argv[i],"-noframelimit") == 0) {
			Config.FrameLimit = 0;
		}

		// frame skip
		if (strcmp(argv[i],"-frameskip") == 0) {
			int val = -1000;
			if (++i < argc) {
				val = atoi(argv[i]);
				if (val >= -1 && val <= 3) {
					Config.FrameSkip = val;
				}
			} else {
				printf("ERROR: missing value for -frameskip\n");
			}

			if (val == -1000) {
				printf("ERROR: -frameskip value must be between -1..3 (-1 is AUTO)\n");
				param_parse_error = true;
				break;
			}
		}

#ifdef GPU_UNAI
		// Render only every other line (looks ugly but faster)
		if (strcmp(argv[i],"-interlace") == 0) {
			gpu_unai_config_ext.ilace_force = 1;
		}

		// Allow 24bpp->15bpp dithering (only polys, only if PS1 game uses it)
		if (strcmp(argv[i],"-dither") == 0) {
			gpu_unai_config_ext.dithering = 1;
		}

		if (strcmp(argv[i],"-nolight") == 0) {
			gpu_unai_config_ext.lighting = 0;
		}

		if (strcmp(argv[i],"-noblend") == 0) {
			gpu_unai_config_ext.blending = 0;
		}

		// Apply lighting to all primitives. Default is to only light primitives
		//  with light values below a certain threshold (for speed).
		if (strcmp(argv[i],"-nofastlight") == 0) {
			gpu_unai_config_ext.fast_lighting = 0;
		}

		// Render all pixels on a horizontal line, even when in hi-res 512,640
		//  PSX vid modes and those pixels would never appear on 320x240 screen.
		//  (when using pixel-dropping downscaler).
		//  Can cause visual artifacts, default is on for now (for speed)
		if (strcmp(argv[i],"-nopixelskip") == 0) {
			gpu_unai_config_ext.pixel_skip = 0;
		}

		// Settings specific to older, non-gpulib standalone gpu_unai:
	#ifndef USE_GPULIB
		// Progressive interlace option - See gpu_unai/gpu.h
		// Old option left in from when PCSX4ALL ran on very slow devices.
		if (strcmp(argv[i],"-progressive") == 0) {
			gpu_unai_config_ext.prog_ilace = 1;
		}
	#endif //!USE_GPULIB
#endif //GPU_UNAI


	// SPU
	#ifndef SPU_NULL

	// ----- BEGIN SPU_PCSXREARMED SECTION -----
	#ifdef SPU_PCSXREARMED
		// No sound
		if (strcmp(argv[i],"-silent") == 0) {
			spu_config.iDisabled = 1;
		}
		// Reverb
		if (strcmp(argv[i],"-reverb") == 0) {
			spu_config.iUseReverb = 1;
		}
		// XA Pitch change support
		if (strcmp(argv[i],"-xapitch") == 0) {
			spu_config.iXAPitch = 1;
		}

		// Enable SPU thread
		// NOTE: By default, PCSX ReARMed would not launch
		//  a thread if only one core was detected, but I have
		//  changed it to allow it under any case.
		// TODO: test if any benefit is ever achieved
		if (strcmp(argv[i],"-threaded_spu") == 0) {
			spu_config.iUseThread = 1;
		}

		// Don't output fixed number of samples per frame
		// (unknown if this helps or hurts performance
		//  or compatibility.) The default in all builds
		//  of PCSX_ReARMed is "true", so that is also the
		//  default here.
		if (strcmp(argv[i],"-nofixedupdates") == 0) {
			spu_config.iUseFixedUpdates = 0;
		}

		// Set interpolation none/simple/gaussian/cubic, default is none
		if (strcmp(argv[i],"-interpolation") == 0) {
			int val = -1;
			if (++i < argc) {
				if (strcmp(argv[i],"none") == 0) val=0;
				if (strcmp(argv[i],"simple") == 0) val=1;
				if (strcmp(argv[i],"gaussian") == 0) val=2;
				if (strcmp(argv[i],"cubic") == 0) val=3;
			} else
				printf("ERROR: missing value for -interpolation\n");


			if (val == -1) {
				printf("ERROR: -interpolation value must be one of: none,simple,gaussian,cubic\n");
				param_parse_error = true; break;
			}

			spu_config.iUseInterpolation = val;
		}

		// Set volume level of SPU, 0-1024
		//  If value is 0, sound will be disabled.
		if (strcmp(argv[i],"-volume") == 0) {
			int val = -1;
			if (++i < argc)
				val = atoi(argv[i]);
			else
				printf("ERROR: missing value for -volume\n");

			if (val < 0 || val > 1024) {
				printf("ERROR: -volume value must be between 0-1024. Value of 0 will mute sound\n"
						"        but SPU plugin will still run, ensuring best compatibility.\n"
						"        Use -silent flag to disable SPU plugin entirely.\n");
				param_parse_error = true; break;
			}

			spu_config.iVolume = val;
		}

		// SPU will issue updates at a rate that ensures better
		//  compatibility, but if the emulator runs too slowly,
		//  audio stutter will be increased. "False" is the
		//  default setting on Pandora/Pyra/Android builds of
		//  PCSX_ReARMed, but Wiz/Caanoo builds used the faster
		//  inaccurate setting, "true", so I've made our default
		//  "true" as well, since we target low-end devices.
		if (strcmp(argv[i],"-notempo") == 0) {
			spu_config.iTempo = 0;
		}

		//NOTE REGARDING ABOVE SETTING "spu_config.iTempo":
		// From thread https://pyra-handheld.com/boards/threads/pcsx-rearmed-r22-now-using-the-dsp.75388/
		// Notaz says that setting iTempo=1 restores pcsxreARMed SPU's old behavior, which allows slow emulation
		// to not introduce audio dropouts (at least I *think* he's referring to iTempo config setting)
		// "Probably the main change is SPU emulation, there were issues in some games where effects were wrong,
		//  mostly Final Fantasy series, it should be better now. There were also sound sync issues where game would
		//  occasionally lock up (like Valkyrie Profile), it should be stable now.
		//  Changed sync has a side effect however - if the emulator is not fast enough (may happen with double
		//  resolution mode or while underclocking), sound will stutter more instead of slowing down the music itself.
		//  There is a new option in SPU plugin config to restore old inaccurate behavior if anyone wants it." -Notaz

	#endif //SPU_PCSXREARMED
	// ----- END SPU_PCSXREARMED SECTION -----

	#endif //!SPU_NULL
	}

	update_memcards(0);
	strcpy(BiosFile, Config.Bios);

	if (param_parse_error) {
		printf("Failed to parse command-line parameters, exiting.\n");
		exit(1);
	}

	//NOTE: spu_pcsxrearmed will handle audio initialization
	SDL_Init(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK | SDL_INIT_NOPARACHUTE);

	atexit(pcsx4all_exit);

#ifdef SDL_TRIPLEBUF
	int flags = SDL_HWSURFACE | SDL_TRIPLEBUF;
#else
	int flags = SDL_HWSURFACE | SDL_DOUBLEBUF;
#endif

	screen = SDL_SetVideoMode(320, 240, 16, flags);
	if (!screen) {
		puts("NO Set VideoMode 320x240x16");
		exit(0);
	}

	if (SDL_MUSTLOCK(screen))
		SDL_LockSurface(screen);

	SDL_WM_SetCaption("pcsx4all - SDL Version", "pcsx4all");

	SCREEN = (Uint16 *)screen->pixels;

	if (argc < 2 || cdrfilename[0] == '\0') {
		// Enter frontend main-menu:
		emu_running = false;
		if (!SelectGame()) {
			printf("ERROR: missing filename for -iso\n");
			exit(1);
		}
	}

	if (psxInit() == -1) {
		printf("PSX emulator couldn't be initialized.\n");
		exit(1);
	}

	if (LoadPlugins() == -1) {
		printf("Failed loading plugins.\n");
		exit(1);
	}

	pcsx4all_initted = true;
	emu_running = true;

	// Initialize plugin_lib, gpulib
	pl_init();

	psxReset();

	if (cdrfilename[0] != '\0') {
		if (CheckCdrom() == -1) {
			printf("Failed checking ISO image.\n");
			SetIsoFile(NULL);
		} else {
			check_spec_bios();
			psxReset();
			printf("Running ISO image: %s.\n", cdrfilename);
			if (LoadCdrom() == -1) {
				printf("Failed loading ISO image.\n");
				SetIsoFile(NULL);
			}
		}
	}

	if (filename[0] != '\0') {
		if (Load(filename) == -1) {
			printf("Failed loading executable.\n");
			filename[0]='\0';
		}
	}

	if (filename[0] != '\0') {
		printf("Running executable: %s.\n",filename);
	}

	if ((cdrfilename[0] == '\0') && (filename[0] == '\0') && (Config.HLE == 0)) {
		printf("Running BIOS.\n");
	}

	if ((cdrfilename[0] != '\0') || (filename[0] != '\0') || (Config.HLE == 0)) {
		psxCpu->Execute();
	}

	return 0;
}

unsigned get_ticks(void)
{
#ifdef TIME_IN_MSEC
	return SDL_GetTicks();
#else
	return ((((unsigned long long)clock())*1000000ULL)/((unsigned long long)CLOCKS_PER_SEC));
#endif
}

void wait_ticks(unsigned s)
{
#ifdef TIME_IN_MSEC
	SDL_Delay(s);
#else
	SDL_Delay(s/1000);
#endif
}

void port_printf(int x, int y, const char *text)
{
	unsigned short *screen = (SCREEN + x + y * 320);
	for (int i = 0; i < strlen(text); i++) {
		for (int l = 0; l < 8; l++) {
			if (fontdata8x8[((text[i])*8)+l]&0x80) screen[l*320+0]=0xffff; //:0x0000;
			if (fontdata8x8[((text[i])*8)+l]&0x40) screen[l*320+1]=0xffff; //:0x0000;
			if (fontdata8x8[((text[i])*8)+l]&0x20) screen[l*320+2]=0xffff; //:0x0000;
			if (fontdata8x8[((text[i])*8)+l]&0x10) screen[l*320+3]=0xffff; //:0x0000;
			if (fontdata8x8[((text[i])*8)+l]&0x08) screen[l*320+4]=0xffff; //:0x0000;
			if (fontdata8x8[((text[i])*8)+l]&0x04) screen[l*320+5]=0xffff; //:0x0000;
			if (fontdata8x8[((text[i])*8)+l]&0x02) screen[l*320+6]=0xffff; //:0x0000;
			if (fontdata8x8[((text[i])*8)+l]&0x01) screen[l*320+7]=0xffff; //:0x0000;
		}
		screen += 8;
	}
}

void port_printf_fg_bg(int x, int y, const char *text, int fg, int bg)
{
	unsigned short *screen = (SCREEN + x + y * 320);
	for (int i = 0; i < strlen(text); i++) {
		for (int l = 0; l < 8; l++) {
			screen[l*320+0] = (fontdata8x8[((text[i])*8)+l]&0x80) ? fg : bg;
			screen[l*320+1] = (fontdata8x8[((text[i])*8)+l]&0x40) ? fg : bg;
			screen[l*320+2] = (fontdata8x8[((text[i])*8)+l]&0x20) ? fg : bg;
			screen[l*320+3] = (fontdata8x8[((text[i])*8)+l]&0x10) ? fg : bg;
			screen[l*320+4] = (fontdata8x8[((text[i])*8)+l]&0x08) ? fg : bg;
			screen[l*320+5] = (fontdata8x8[((text[i])*8)+l]&0x04) ? fg : bg;
			screen[l*320+6] = (fontdata8x8[((text[i])*8)+l]&0x02) ? fg : bg;
			screen[l*320+7] = (fontdata8x8[((text[i])*8)+l]&0x01) ? fg : bg;
		}
		screen += 8;
	}
}
