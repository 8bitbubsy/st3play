#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "../../dig.h"
#include "../../mixer/sbpro.h"
#include "../../mixer/gus_gf1.h"
#include "posix.h"

#define CLAMP(x, low, high) (((x) > (high)) ? (high) : (((x) < (low)) ? (low) : (x)))

// defaults when not overridden by argument switches
#define DEFAULT_MIX_FREQ 48000
#define DEFAULT_MIX_BUFSIZE 1024
#define DEFAULT_MIX_VOL 256
#define DEFAULT_SOUNDCARD -1 /* -1 (auto-detect), SOUNDCARD_SBPRO or SOUNDCARD_GUS */

// set to true if you want st3play to always render to WAV
#define DEFAULT_WAVRENDER_MODE_FLAG false

// default settings
bool renderToWavFlag = DEFAULT_WAVRENDER_MODE_FLAG;
static int32_t soundCardType = DEFAULT_SOUNDCARD;
static int32_t mixingVolume = DEFAULT_MIX_VOL;
static int32_t mixingFrequency = DEFAULT_MIX_FREQ;
static int32_t mixingBufferSize = DEFAULT_MIX_BUFSIZE;
// ----------------------------------------------------------

static volatile bool programRunning;
static char *filename, *WAVRenderFilename;

static void showUsage(void);
static void handleArguments(int argc, char *argv[]);
static void readKeyboard(void);
static int32_t renderToWav(void);

// yuck!
#ifdef _WIN32
void wavRecordingThread(void *arg)
#else
void *wavRecordingThread(void *arg)
#endif
{
	Dig_renderToWAV(mixingFrequency, mixingBufferSize, WAVRenderFilename);
#ifndef _WIN32
	return NULL;
#endif
	(void)arg;
}

#ifndef _WIN32
static void sigtermFunc(int32_t signum)
{
	programRunning = false; // unstuck main loop
	WAVRender_Flag = false; // unstuck WAV render loop
	(void)signum;
}
#endif

int main(int argc, char *argv[])
{
#ifdef _DEBUG
	filename = "debug.s3m";
	(void)argc;
	(void)argv;
#else
	if (argc < 2 || (argc == 2 && (!strcmp(argv[1], "/?") || !strcmp(argv[1], "-h"))))
	{
		showUsage();
		return 1;
	}

	handleArguments(argc, argv);
#endif

	audio.fMixingVol = mixingVolume / (256.0f / 32768.0f);

	if (!initMusic(mixingFrequency, mixingBufferSize))
	{
		printf("Error: Out of memory while setting up replayer!\n");
		return 1;
	}

	if (!load_st3(filename, soundCardType))
	{
		printf("Error: Couldn't load song!\n");
		return 1;
	}

	if (soundCardType == -1)
	{
		printf("NOTE:\n");
		printf("  Song was analyzed, and sound card of choice was set to '%s'.\n",
			(audio.soundcardtype == SOUNDCARD_GUS) ? "Gravis Ultrasound" : "Sound Blaster Pro");
		printf("  This detection can sometimes be infeasible, but it can be overridden by using\n");
		printf("  the -s switch from the command line. 'st3play -h' for more info on this.\n\n");
	}

	// trap sigterm on Linux/macOS (since we need to properly revert the terminal)
#ifndef _WIN32
	struct sigaction action;
	memset(&action, 0, sizeof (struct sigaction));
	action.sa_handler = sigtermFunc;
	sigaction(SIGTERM, &action, NULL);
#endif

	zplaysong(0);
	if (renderToWavFlag)
		return renderToWav();

	printf("Playing, press ESC to stop...\n");
	printf("\n");
	printf("Controls:\n");
	printf("Esc=Quit   Space=Toggle Pause   Plus = inc. song pos   Minus = dec. song pos\n");
	printf("\n");
	printf("Name: %s\n", song.header.name);
	printf("Instruments: %d/99\n", song.header.insnum);
	printf("Song length: %d/255\n", song.header.ordnum);

	if (audio.soundcardtype == SOUNDCARD_GUS)
		printf("Sound card: Gravis Ultrasound\n");
	else
		printf("Sound card: Sound Blaster Pro (%s)\n",  SBPro_StereoMode() ? "stereo" : "mono");

	printf("Internal mixing frequency: %.2fHz\n", (audio.soundcardtype == SOUNDCARD_GUS) ? GUS_GetOutputRate() : SBPro_GetOutputRate());
	printf("Audio output frequency: %dHz\n", audio.outputFreq);
	printf("ST3 stereo mode: %s\n", song.stereomode ? "Yes" : "No");
	if (audio.soundcardtype == SOUNDCARD_SBPRO)
		printf("Master volume: %d/127\n", audio.mastermul);
	printf("\n");

	printf("- STATUS -\n");

#ifndef _WIN32
	modifyTerminal();
#endif

	programRunning = true;
	while (programRunning)
	{
		readKeyboard();

		if (audio.soundcardtype == SOUNDCARD_GUS)
		{
			if (song.adlibused)
			{
				printf(" Pos: %03d/%03d - Pat: %02d - Row: %02d/64 - Active GUS voices: %02d - Active AdLib voices: %01d/9 %s\r",
					song.np_ord, song.header.ordnum, song.np_pat, song.np_row,
					activePCMVoices(), activeAdLibVoices(), !audio.playing ? "(PAUSED)" : "        ");
			}
			else
			{
				printf(" Pos: %03d/%03d - Pat: %02d - Row: %02d/64 - Active GUS voices: %02d %s\r",
					song.np_ord, song.header.ordnum, song.np_pat, song.np_row,
					activePCMVoices(), !audio.playing ? "(PAUSED)" : "        ");
			}
		}
		else
		{
			if (song.adlibused)
			{
				printf(" Pos: %03d/%03d - Pat: %02d - Row: %02d/64 - Active ST3 PCM voices: %02d/16 - Active AdLib voices: %01d/9 %s\r",
					song.np_ord, song.header.ordnum, song.np_pat, song.np_row,
					activePCMVoices(), activeAdLibVoices(), !audio.playing ? "(PAUSED)" : "        ");
			}
			else
			{
				printf(" Pos: %03d/%03d - Pat: %02d - Row: %02d/64 - Active ST3 PCM voices: %02d/16 %s\r",
					song.np_ord, song.header.ordnum, song.np_pat, song.np_row,
					activePCMVoices(), !audio.playing ? "(PAUSED)" : "        ");
			}
		}

		fflush(stdout);
		Sleep(25);
	}

#ifndef _WIN32
	revertTerminal();
#endif

	printf("\n");

	closeMusic();

	printf("Playback stopped.\n");
	return 0;
}

static void showUsage(void)
{
	printf("Usage:\n");
	printf("  st3play input_module [-f hz] [-s sb/gus] [-b buffersize]\n");
	printf("  st3play input_module [--no-intrp] [--render-to-wav]\n");
	printf("\n");
	printf("  Options:\n");
	printf("    input_module     Specifies the module file to load (.S3M)\n");
	printf("    -f hz            Specifies the output frequency (8000..384000)\n");
	printf("    -m mixingvol     Specifies the mixing volume (0..256)\n");
	printf("    -s sb/gus        Skips sound card detection and uses sb (SB Pro) or gus.\n");
	printf("                     Certain behaviors in the replayer depends on the sound card\n");
	printf("                     type. GUS also has volume ramping.\n");
	printf("    -b buffersize    Specifies the mixing buffer size (256..8192)\n");
	printf("    --render-to-wav  Renders song to WAV instead of playing it. The output\n");
	printf("                     filename will be the input filename with .WAV added to the\n");
	printf("                     end.\n");
	printf("\n");
	printf("Default settings:\n");
	printf("  - Mixing buffer size:       %d\n", DEFAULT_MIX_BUFSIZE);
	printf("  - Mixing volume:            %d\n", DEFAULT_MIX_VOL);
	printf("  - Sound card type:          %s\n",
		(soundCardType == -1) ? "Auto-Detect" :
		((soundCardType == SOUNDCARD_GUS) ? "Gravis Ultrasound" : "Sound Blaster Pro"));
	printf("  - WAV render mode:          %s\n", DEFAULT_WAVRENDER_MODE_FLAG ? "On" : "Off");
	printf("\n");
}

static void handleArguments(int argc, char *argv[])
{
	filename = argv[1];
	if (argc > 2) // parse arguments
	{
		for (int32_t i = 1; i < argc; i++)
		{
			if (!_stricmp(argv[i], "-f") && i+1 < argc)
			{
				const int32_t num = atoi(argv[i+1]);
				mixingFrequency = CLAMP(num, 8000, 384000);
			}
			else if (!_stricmp(argv[i], "-m") && i+1 < argc)
			{
				const int32_t num = atoi(argv[i+1]);
				mixingVolume = CLAMP(num, 0, 256);
			}
			else if (!_stricmp(argv[i], "-s") && i+1 < argc)
			{
				if (!_stricmp(argv[i+1],  "sb")) soundCardType = SOUNDCARD_SBPRO;
				if (!_stricmp(argv[i+1], "gus")) soundCardType = SOUNDCARD_GUS;
			}
			else if (!_stricmp(argv[i], "-b") && i+1 < argc)
			{
				const int32_t num = atoi(argv[i+1]);
				mixingBufferSize = CLAMP(num, 256, 8192);
			}
			else if (!_stricmp(argv[i], "--render-to-wav"))
			{
				renderToWavFlag = true;
			}
		}
	}
}

static void readKeyboard(void)
{
	if (_kbhit())
	{
		const int32_t key = _getch();
		switch (key)
		{
			case 0x1B: // esc
				programRunning = false;
			break;

			case 0x20: // space
				togglePause();
			break;

			case 0x2B: // numpad +
				shutupsounds();
				zgotosong((song.np_ord + 0) & 0xFF, 0);
			break;

			case 0x2D: // numpad -
				shutupsounds();
				zgotosong((song.np_ord - 2) & 0xFF, 0);
			break;
			
			default: break;
		}
	}
}

static int32_t renderToWav(void)
{
	const size_t filenameLen = strlen(filename);
	WAVRenderFilename = (char *)malloc(filenameLen+5);

	if (WAVRenderFilename == NULL)
	{
		printf("Error: Out of memory!\n");
		closeMusic();
		return 1;
	}

	strcpy(WAVRenderFilename, filename);
	strcat(WAVRenderFilename, ".wav");

	/* The WAV render loop also sets/listens/clears "WAVRender_Flag", but let's set it now
	** since we're doing the render in a separate thread (to be able to force-abort it if
	** the user is pressing a key).
	**
	** If you don't want to create a thread for the render, you don't have to
	** set this flag, and you just call Dig_RenderToWAV("output.wav") directly.
	** Though, some songs will render forever (if they Bxx-jump to a previous order),
	** thus having this in a thread is recommended so that you can force-abort it, if stuck.
	*/
	WAVRender_Flag = true;
	if (!createSingleThread(wavRecordingThread))
	{
		printf("Error: Couldn't create WAV rendering thread!\n");
		free(WAVRenderFilename);
		closeMusic();
		return 1;
	}

	printf("Rendering to WAV. If stuck forever, press any key to stop rendering...\n");

#ifndef _WIN32
	modifyTerminal();
#endif
	while (WAVRender_Flag)
	{
		Sleep(200);
		if ( _kbhit())
			WAVRender_Flag = false;
	}
#ifndef _WIN32
	revertTerminal();
#endif

	closeSingleThread();

	free(WAVRenderFilename);
	closeMusic();

	return 0;
}
