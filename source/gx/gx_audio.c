/****************************************************************************
 *  gx_audio.c
 *
 *  Genesis Plus GX audio support
 *
 *  code by Eke-Eke (2007,2008)
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 ***************************************************************************/

#include "shared.h"

#include <asndlib.h>
#include <oggplayer.h>

/* DMA soundbuffers (required to be 32-bytes aligned)
   Length is dimensionned for one frame of emulation (see below)
   To prevent audio clashes, we use double buffering technique:
    one buffer is the active DMA buffer
    the other one is the current work buffer (updated during frame emulation)
   We do not need more since frame emulation and DMA operation are synchronized
*/
u8 soundbuffer[2][3840] ATTRIBUTE_ALIGN(32);

/* Current work soundbuffer */
u8 mixbuffer;

/* Next DMA length */
static u32 dma_len;

/* Current delta between output & expected sample counts */
static int delta;

/* Expected sample count x 100 */
static u32 dma_sync;

/* audio DMA status */
static u8 audioStarted = 0;

/* Background music */
static u8 *Bg_music_ogg = NULL;
static u32 Bg_music_ogg_size = 0;

/*** 
      AudioDmaCallback

     In 50Hz emulation mode, we synchronize frame emulation with audio DMA
     50Hz VSYNC period is shorter than DMA period so there is no video frameskipping
     In 60Hz modes, VSYNC period is longer than default DMA period so it requires different sync.
 ***/

static void AudioDmaCallback(void)
{
  frameticker++;
}

/***
      gx_audio__init

     This function initializes the Audio Interface
     Default samplerate is set to 48khZ
 ***/
void gx_audio_Init(void)
{
  AUDIO_Init (NULL);
  AUDIO_SetDSPSampleRate (AI_SAMPLERATE_48KHZ);

  /* load background music from FAT device */
  char fname[MAXPATHLEN];
  sprintf(fname,"%s/Bg_music.ogg",DEFAULT_PATH);
  FILE *f = fopen(fname,"rb");
  if (f)
  {
    struct stat filestat;
    stat(fname, &filestat);
    Bg_music_ogg_size = filestat.st_size;
    Bg_music_ogg = memalign(32,Bg_music_ogg_size);
    if (Bg_music_ogg) fread(Bg_music_ogg,1,Bg_music_ogg_size,f);
    fclose(f);
  }
}

void gx_audio_Shutdown(void)
{
  if (Bg_music_ogg) free(Bg_music_ogg);
}

/*** 
      gx_audio__update

     This function is called at the end of each frame
     Genesis Plus only provides sound data on completion of each frame.
     DMA sync and switching ensure we never access the active DMA buffer and sound clashes never happen
     This function retrieves samples for the frame then set the next DMA parameters 
     Parameters will be taken in account only when current DMA operation is over
 ***/
void gx_audio_Update(void)
{
  u32 size = dma_len;
  
  /* get audio samples */
  audio_update(dma_len);

  /* update DMA parameters */
  s16 *sb = (s16 *)(soundbuffer[mixbuffer]);
  mixbuffer ^= 1;
  size = size << 2;
  DCFlushRange((void *)sb, size);
  AUDIO_InitDMA((u32) sb, size);

  /* Start Audio DMA */
  /* this is only called once, DMA is automatically restarted when previous one is over */
  /* When DMA parameters are not updated, same soundbuffer is played again */
  if (!audioStarted)
  {
    audioStarted = 1;
    AUDIO_StartDMA();
    if (frameticker > 1) frameticker = 1;
  }

  /* VIDEO interrupt sync: we approximate DMA length (see below) */
  /* DMA length should be 32 bytes so we use 800 or 808 samples */
  if (dma_sync)
  {
    delta += (dma_len * 100) - dma_sync;
    if (delta < 0) dma_len = 808;
    else dma_len = 800;
  }
}

/*** 
      gx_audio__start

     This function resets the audio engine
     This is called when coming back from Main Menu
 ***/
void gx_audio_Start(void)
{
  /* shutdown menu audio */
  PauseOgg(1);
  StopOgg();
  ASND_Pause(1);
  ASND_End();

  /* initialize default DMA length */
  /* PAL (50Hz): 20000 us period --> 960 samples/frame  @48kHz */
  /* NTSC (60Hz): 16667 us period --> 800 samples/frame @48kHz */
  dma_len   = vdp_pal ? 960 : 800;
  dma_sync  = 0;
  mixbuffer = 0;
  delta     = 0;

  /* reset sound buffers */
  memset(soundbuffer, 0, 2 * 3840);

  /* default */
  AUDIO_SetDSPSampleRate (AI_SAMPLERATE_48KHZ);
  AUDIO_RegisterDMACallback(NULL);

  /* let's use audio DMA to synchronize frame emulation */
  if (vdp_pal | gc_pal) AUDIO_RegisterDMACallback(AudioDmaCallback);

  /* 60hz video mode requires synchronization with Video interrupt      */
  /* VSYNC period is 16715 us which is approx. 802.32 samples           */
  /* to prevent audio/video desynchronization, we approximate the exact */
  /* number of samples by using alternate audio length                  */
  else dma_sync = 80232;
}

/***
      gx_audio__stop

     This function stops current Audio DMA process
     This is called when going back to Main Menu
     DMA need to be restarted when going back to the game (see above)
 ***/
void gx_audio_Stop(void)
{
  /* stop emulator audio */
  AUDIO_StopDMA ();
  audioStarted = 0;

  /* restart menu audio */
  ASND_Init();
  ASND_Pause(0);
  if (Bg_music_ogg)
  {
    PauseOgg(0);
    PlayOgg(mem_open((char *)Bg_music_ogg, Bg_music_ogg_size), 0, OGG_INFINITE_TIME);
    SetVolumeOgg(((int)config.bgm_volume * 255) / 100);
  }
}
