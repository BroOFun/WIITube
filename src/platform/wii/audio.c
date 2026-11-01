/* ASND double-buffers one voice: while block N plays, block N+1 waits in the second slot.
 * When the second slot empties (N+1 started), ASND calls voice_cb, which fills the next
 * block from a lock-free ring written by the main thread. The block that just started is
 * remembered with its timestamp to give an accurate playback clock. */
#include <gccore.h>
#include <asndlib.h>
#include <malloc.h>
#include <ogc/lwp_watchdog.h>
#include <ogc/machine/processor.h>
#include <string.h>

#include "audio.h"

#define VOICE 0
#define BLOCK_FRAMES 2048 /* ~46 ms at 44.1 kHz */
#define BLOCK_BYTES (BLOCK_FRAMES * 4)
#define NBLOCKS 4
#define RING_FRAMES 65536 /* ~1.5 s; power of two */

#define barrier() __asm__ __volatile__("" ::: "memory")

typedef struct {
    u32 frame0; /* index of the first ring frame in this block */
    u32 len;    /* real frames (rest is silence) */
} block_info;

static u32 ring[RING_FRAMES];         /* one stereo s16 frame per u32 */
static volatile u32 ring_wr, ring_rd; /* frame counters; wr by main thread, rd by callback */
static u8 *blocks[NBLOCKS];
static block_info binfo[NBLOCKS];

static volatile int cur_block, next_block;
static volatile u32 cur_frame0, cur_len;
static volatile u64 cur_tick;
static volatile u32 underruns;

static int rate = 44100;
static double base_pts;
static int have_base, playing, paused;
static u64 pause_tick;
static int volume = 255;

void audio_init(void)
{
    int i;
    ASND_Init();
    ASND_Pause(0);
    for (i = 0; i < NBLOCKS; i++)
        blocks[i] = memalign(32, BLOCK_BYTES);
}

static void fill_block(int i)
{
    u32 rd = ring_rd, avail = ring_wr - rd, n, k;
    u32 *dst = (u32 *)blocks[i];
    n = avail < BLOCK_FRAMES ? avail : BLOCK_FRAMES;
    for (k = 0; k < n; k++)
        dst[k] = ring[(rd + k) & (RING_FRAMES - 1)];
    if (n < BLOCK_FRAMES)
        memset(dst + n, 0, (BLOCK_FRAMES - n) * 4);
    binfo[i].frame0 = rd;
    binfo[i].len = n;
    if (n == 0 && playing && !paused)
        underruns++;
    barrier();
    ring_rd = rd + n;
    DCFlushRange(blocks[i], BLOCK_BYTES);
}

static void voice_cb(s32 voice)
{
    int i;
    if (next_block >= 0) { /* the queued block has started playing */
        cur_block = next_block;
        cur_frame0 = binfo[cur_block].frame0;
        cur_len = binfo[cur_block].len;
        cur_tick = gettime();
    }
    i = (cur_block + 1) % NBLOCKS;
    fill_block(i);
    next_block = ASND_AddVoice(voice, blocks[i], BLOCK_BYTES) == SND_OK ? i : -1;
}

void audio_reset(int sample_rate)
{
    audio_stop();
    rate = sample_rate > 0 ? sample_rate : 44100;
    ring_wr = ring_rd = 0;
    have_base = 0;
    base_pts = 0;
    underruns = 0;
}

int audio_space(void) { return (int)(RING_FRAMES - (ring_wr - ring_rd)); }
int audio_buffered(void) { return (int)(ring_wr - ring_rd); }

void audio_push(const int16_t *pcm, int frames, double pts)
{
    const u32 *src = (const u32 *)pcm;
    u32 wr = ring_wr;
    int k;
    if (!have_base) {
        base_pts = pts;
        have_base = 1;
    }
    for (k = 0; k < frames; k++)
        ring[(wr + k) & (RING_FRAMES - 1)] = src[k];
    barrier();
    ring_wr = wr + (u32)frames;
}

void audio_play(void)
{
    if (playing)
        return;
    cur_block = 0;
    next_block = -1;
    fill_block(0);
    cur_frame0 = binfo[0].frame0;
    cur_len = binfo[0].len;
    cur_tick = gettime();
    playing = 1;
    paused = 0;
    ASND_SetVoice(VOICE, VOICE_STEREO_16BIT, rate, 0, blocks[0], BLOCK_BYTES, volume, volume, voice_cb);
}

void audio_pause(int p)
{
    if (!playing || p == paused)
        return;
    if (p) {
        pause_tick = gettime();
        ASND_PauseVoice(VOICE, 1);
    } else {
        cur_tick += gettime() - pause_tick;
        ASND_PauseVoice(VOICE, 0);
    }
    paused = p;
}

void audio_stop(void)
{
    if (playing)
        ASND_StopVoice(VOICE);
    playing = 0;
    paused = 0;
}

double audio_clock(void)
{
    u32 f0, len, irq;
    u64 t0, now;
    double played;

    if (!playing)
        return -1;
    _CPU_ISR_Disable(irq); /* read the callback's state consistently */
    f0 = cur_frame0;
    len = cur_len;
    t0 = cur_tick;
    _CPU_ISR_Restore(irq);
    now = paused ? pause_tick : gettime();
    played = (double)diff_usec(t0, now) * rate / 1e6;
    if (played > len)
        played = len;
    return base_pts + ((double)f0 + played) / rate;
}

void audio_set_volume(int v)
{
    volume = v < 0 ? 0 : v > 255 ? 255 : v;
    if (playing)
        ASND_ChangeVolumeVoice(VOICE, volume, volume);
}

int audio_underruns(void) { return (int)underruns; }
