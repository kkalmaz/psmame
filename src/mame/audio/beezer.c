/*************************************************************************

    Beezer 6522+6840 audio hardware
    driver based on exidy audio hardware which it shares some similarity to
    Original driver by ? (Nicola or Aaron, I think)
    Preliminary, partially functional driver by Jonathan Gevaryahu
    AKA Lord Nightmare

    TODO:
    * Several inexplicable things on the schematic are not handled, such as the
     'VCO' input for 6840 channel 2 external clock whose source does not appear
     anywhere on the schematic, nor does it handle the 'DMOD DATA' and 'DMOD
     CLR' outputs for some sort of digital modulator (perhaps an hc55516?)
     from the VIA, which also does not appear anywhere on schematic.
     The latter the VIA *DOES* seem to write something to, but it may be just
     a silence waveform for 55516, alternating zeroes and ones.
    * The channel mixing is done additively at the moment rather than
     emulating the original multiplexer, which is actually not that hard to do
    * The 'FM OR AM' output of the audio via (pb1) appears to control some sort
     of suppression or filtering change of the post-DAC amplifier when enabled,
     only during the TIMER1 OUT time-slot of the multiplexer, see page 1B 3-3
     of schematics. This will be a MESS to emulate since theres a lot of analog
     crap involved.
    * The /INT line and related logic of the 6840 is not emulated, and should
     be hooked to the audio 6809
    * Convert this to a modern device instead of a deprecated old style device


    Notes on multiplexer:
    The sound output is from a DAC76xx 8-bit dac; driving this dac is a pair
     of 74ls670 4x4 register files wired up as four 8-bit words;
     The four words are used as the volumes for four 1-bit channels by inversion
     of the MSB of the 8 bit value.
    The four channels are:
    CNT1 CNT0
    0    0    6522 pin 7 output (squarewave); 'FM or AM' affects this slot only
    0    1    6840 channel 1 clocked by E1(int) OR by 6522 PB7-latched NOISE
    1    0    6840 channel 2 clocked by E1(int) OR by "VCO" ext (Huh?)
    1    1    6840 channel 3 clocked by E1(int) OR by channel 2-latched NOISE

    The four slots determine which address is selected of the 8-bit words and
     which source will XOR against the MSB of the 8-bit word before it goes
     to the DAC (effectively making the 8-bit word be a 7-bit volume control
     plus optional wave invert). The speed which cnt0 and cnt1 count at is
     E1/16 or 62500Hz.

*************************************************************************/

#include "emu.h"
#include "machine/rescap.h"
#include "cpu/m6809/m6809.h"
#include "includes/beezer.h"



/*************************************
 *
 *  Constants
 *
 *************************************/

#define CRYSTAL_OSC				(XTAL_12MHz)
#define SH6840_CLOCK			(CRYSTAL_OSC / 12)
#define MULTIPLEX_FREQ			(SH6840_CLOCK / 16)

/*************************************
 *
 *  Local variables
 *
 *************************************/

/* 6840 variables */
struct sh6840_timer_channel
{
	UINT8	cr;
	UINT8	state;
	UINT8	leftovers;
	UINT16	timer;
	UINT32	clocks;
	UINT8	int_flag;
	union
	{
#ifdef LSB_FIRST
		struct { UINT8 l, h; } b;
#else
		struct { UINT8 h, l; } b;
#endif
		UINT16 w;
	} counter;
};

typedef struct _beezer_sound_state beezer_sound_state;
struct _beezer_sound_state
{
	device_t *m_maincpu;

	/* IRQ variable */
	UINT8 m_ptm_irq_state;

	struct sh6840_timer_channel m_sh6840_timer[3];
	UINT8 m_sh6840_volume[4];
	UINT8 m_sh6840_MSB_latch;
	UINT8 m_sh6840_LSB_latch;
	UINT32 m_sh6840_LFSR;
	UINT32 m_sh6840_LFSR_clocks;
	UINT32 m_sh6840_clocks_per_sample;
	UINT32 m_sh6840_clock_count;

	UINT32 m_sh6840_latchwrite;
	UINT32 m_sh6840_latchwriteold;
	UINT32 m_sh6840_noiselatch1;
	UINT32 m_sh6840_noiselatch3;

	/* sound streaming variables */
	sound_stream *m_stream;
	double m_freq_to_step;

};


INLINE beezer_sound_state *get_safe_token(device_t *device)
{
	assert(device != NULL);
	assert(device->type() == BEEZER);

	return (beezer_sound_state *)downcast<legacy_device_base *>(device)->token();
}

/*************************************
 *
 *  Interrupt generation helper
 *  TODO: make this actually do something useful
 *************************************/

/*static WRITE_LINE_DEVICE_HANDLER( update_irq_state )
{
    beezer_sound_state *sndstate = get_safe_token(device);
    cputag_set_input_line(device->machine(), "audiocpu", M6809_IRQ_LINE, (sndstate->ptm_irq_state) ? ASSERT_LINE : CLEAR_LINE);
}*/



/*************************************
 *
 *  6840 clock counting helper
 *
 *************************************/
// need to set int_flag properly here
INLINE void sh6840_apply_clock(struct sh6840_timer_channel *t, int clocks)
{
	/* dual 8-bit case */
	if (t->cr & 0x04)
	{
		/* handle full decrements */
		while (clocks > t->counter.b.l)
		{
			clocks -= t->counter.b.l + 1;
			t->counter.b.l = t->timer;

			/* decrement MSB */
			if (!t->counter.b.h--)
			{
				t->state = 0;
				t->counter.w = t->timer;
			}

			/* state goes high when MSB is 0 */
			else if (!t->counter.b.h)
			{
				t->state = 1;
				t->clocks++;
			}
		}

		/* subtract off the remainder */
		t->counter.b.l -= clocks;
	}

	/* 16-bit case */
	else
	{
		/* handle full decrements */
		while (clocks > t->counter.w)
		{
			clocks -= t->counter.w + 1;
			t->state ^= 1;
			t->clocks += t->state;
			t->counter.w = t->timer;
		}

		/* subtract off the remainder */
		t->counter.w -= clocks;
	}
}



/*************************************
 *
 *  Noise generation helper
 *
 *************************************/

INLINE int sh6840_update_noise(beezer_sound_state *state, int clocks)
{
	UINT32 newxor;
	int noise_clocks = 0;
	int i;

	/* loop over clocks */
	for (i = 0; i < clocks; i++)
	{
		state->m_sh6840_LFSR_clocks++;
		if (state->m_sh6840_LFSR_clocks >= 10) // about 10 clocks per 6840 clock
		{
			state->m_sh6840_LFSR_clocks = 0;
			/* shift the LFSR. finally or in the result and see if we've
            * had a 0->1 transition */
			newxor = (((state->m_sh6840_LFSR&0x10000)?1:0) ^ ((state->m_sh6840_LFSR&0x2000)?1:0))?1:0;
			state->m_sh6840_LFSR <<= 1;
			state->m_sh6840_LFSR |= newxor;
			/*printf("LFSR: %4x, %4x, %4x, %4x\n", sh6840_LFSR_3, sh6840_LFSR_2, sh6840_LFSR_1, sh6840_LFSR_0);*/
			/* if we clocked 0->1, that will serve as an external clock */
			if ((state->m_sh6840_LFSR & 0x01) == 0x01) /* tap is at bit 0, GUESSED */
			{
				noise_clocks++;
			}
		}
	}
	return noise_clocks;
}



/*************************************
 *
 *  6840 state saving
 *
 *************************************/

static void sh6840_register_state_globals(device_t *device)
{
	beezer_sound_state *state = get_safe_token(device);

	device->save_item(NAME(state->m_sh6840_volume));
	device->save_item(NAME(state->m_sh6840_MSB_latch));
	device->save_item(NAME(state->m_sh6840_LSB_latch));
	device->save_item(NAME(state->m_sh6840_LFSR));
	device->save_item(NAME(state->m_sh6840_LFSR_clocks));
	device->save_item(NAME(state->m_sh6840_clock_count));
	device->save_item(NAME(state->m_sh6840_latchwrite));
	device->save_item(NAME(state->m_sh6840_latchwriteold));
	device->save_item(NAME(state->m_sh6840_noiselatch1));
	device->save_item(NAME(state->m_sh6840_noiselatch3));
	device->save_item(NAME(state->m_sh6840_timer[0].cr));
	device->save_item(NAME(state->m_sh6840_timer[0].state));
	device->save_item(NAME(state->m_sh6840_timer[0].leftovers));
	device->save_item(NAME(state->m_sh6840_timer[0].timer));
	device->save_item(NAME(state->m_sh6840_timer[0].clocks));
	device->save_item(NAME(state->m_sh6840_timer[0].counter.w));
	device->save_item(NAME(state->m_sh6840_timer[1].cr));
	device->save_item(NAME(state->m_sh6840_timer[1].state));
	device->save_item(NAME(state->m_sh6840_timer[1].leftovers));
	device->save_item(NAME(state->m_sh6840_timer[1].timer));
	device->save_item(NAME(state->m_sh6840_timer[1].clocks));
	device->save_item(NAME(state->m_sh6840_timer[1].counter.w));
	device->save_item(NAME(state->m_sh6840_timer[2].cr));
	device->save_item(NAME(state->m_sh6840_timer[2].state));
	device->save_item(NAME(state->m_sh6840_timer[2].leftovers));
	device->save_item(NAME(state->m_sh6840_timer[2].timer));
	device->save_item(NAME(state->m_sh6840_timer[2].clocks));
	device->save_item(NAME(state->m_sh6840_timer[2].counter.w));
}



/*************************************
 *
 *  Core sound generation
 *
 *************************************/

static STREAM_UPDATE( beezer_stream_update )
{
	beezer_sound_state *state = get_safe_token(device);
	struct sh6840_timer_channel *sh6840_timer = state->m_sh6840_timer;

	/* hack to skip the expensive lfsr noise generation unless at least one of the 2 channels which actually depend on it are set to use it as a source */
	int noisy = ((sh6840_timer[0].cr & sh6840_timer[2].cr & 0x02) == 0);
	stream_sample_t *buffer = outputs[0];

	/* loop over samples */
	while (samples--)
	{
		struct sh6840_timer_channel *t;
		int clocks_this_sample;
		int clocks;
		INT16 sample1, sample2, sample3, sample0;
		INT16 sample = 0;
		sample1 = sample2 = sample3 = sample0 = 0;

		/* determine how many 6840 clocks this sample */
		state->m_sh6840_clock_count += state->m_sh6840_clocks_per_sample;
		clocks_this_sample = state->m_sh6840_clock_count >> 24;
		state->m_sh6840_clock_count &= (1 << 24) - 1;

		/* skip if nothing enabled */
		if ((sh6840_timer[0].cr & 0x01) == 0) // if we're not in reset...
		{
			int noise_clocks_this_sample = 0;
			UINT32 chan1_clocks;

			/* generate noise if configured to do so */
			if (noisy != 0)
				noise_clocks_this_sample = sh6840_update_noise(state, clocks_this_sample);

			/* handle timer 0 if enabled */
			t = &sh6840_timer[0];
			clocks = (t->cr & 0x02) ? clocks_this_sample : state->m_sh6840_noiselatch1;
			sh6840_apply_clock(t, clocks);
			sample1 = (t->state && (t->cr & 0x80))?1:0;

			/* handle timer 1 if enabled */
			t = &sh6840_timer[1];
			chan1_clocks = t->clocks;
			clocks = (t->cr & 0x02) ? clocks_this_sample : 0; // TODO: this is WRONG: channel 1 is clocked by a mystery "VCO CLOCK" signal if not set to E clock. it may not even be connected to anything!
			sh6840_apply_clock(t, clocks);
			sample2 = (t->state && (t->cr & 0x80))?1:0;

			/* generate channel 1-clocked noise if configured to do so */
			if (noisy != 0)
			{
				noise_clocks_this_sample = sh6840_update_noise(state, t->clocks - chan1_clocks);
				if (clocks) state->m_sh6840_noiselatch3 = (state->m_sh6840_LFSR&0x1);
			}

			/* handle timer 2 if enabled */
			t = &sh6840_timer[2];
			clocks = (t->cr & 0x02) ? clocks_this_sample : state->m_sh6840_noiselatch3;
			/* prescale */
			if (t->cr & 0x01)
			{
				clocks += t->leftovers;
				t->leftovers = clocks % 8;
				clocks /= 8;
			}
			sh6840_apply_clock(t, clocks);
			sample3 = (t->state && (t->cr & 0x80))?1:0;
		}
		sample0 = state->m_sh6840_latchwrite?1:0;

		/* stash */
		/* each sample feeds an xor bit on the sign bit of a sign-magnitude (NOT 2'S COMPLEMENT)
         * DAC. This requires some rather convoluted processing:
         * samplex*0x80 brings the sample to the sign bit
         * state->m_sh6840_volume[x]&0x80 pulls the sign bit from the dac sample
         * state->m_sh6840_volume[x]&0x7F pulls the magnitude from the dac sample
         */
		sample += (((sample0*0x80)^(state->m_sh6840_volume[0]&0x80))?-1:1)*(state->m_sh6840_volume[0]&0x7F);
		sample += (((sample1*0x80)^(state->m_sh6840_volume[1]&0x80))?-1:1)*(state->m_sh6840_volume[1]&0x7F);
		sample += (((sample2*0x80)^(state->m_sh6840_volume[2]&0x80))?-1:1)*(state->m_sh6840_volume[2]&0x7F);
		sample += (((sample3*0x80)^(state->m_sh6840_volume[3]&0x80))?-1:1)*(state->m_sh6840_volume[3]&0x7F);
		*buffer++ = sample*64; // adding 3 numbers ranging from -128 to 127 yields a range of -512 to 508; to scale that to '-32768 to 32767' we multiply by 64
	}
}



/*************************************
 *
 *  Audio startup routines
 *
 *************************************/

static DEVICE_START( common_sh_start )
{
	beezer_sound_state *state = get_safe_token(device);
	int sample_rate = MULTIPLEX_FREQ;

	state->m_sh6840_clocks_per_sample = (int)(((double)SH6840_CLOCK / (double)sample_rate) * (double)(1 << 24));

	/* allocate the stream */
	state->m_stream = device->machine().sound().stream_alloc(*device, 0, 1, sample_rate, NULL, beezer_stream_update);
	state->m_maincpu = device->machine().device("maincpu");

	sh6840_register_state_globals(device);
}

static DEVICE_START( beezer_sound )
{
	//beezer_sound_state *state = get_safe_token(device);
	DEVICE_START_CALL(common_sh_start);
}

DEFINE_LEGACY_SOUND_DEVICE(BEEZER, beezer_sound);

/*************************************
 *
 *  Audio reset routines
 *
 *************************************/

static DEVICE_RESET( common_sh_reset )
{
	beezer_sound_state *state = get_safe_token(device);

	/* 6840 */
	memset(state->m_sh6840_timer, 0, sizeof(state->m_sh6840_timer));
	state->m_sh6840_MSB_latch = 0;
	state->m_sh6840_LSB_latch = 0;
	state->m_sh6840_volume[0] = 0;
	state->m_sh6840_volume[1] = 0;
	state->m_sh6840_volume[2] = 0;
	state->m_sh6840_volume[3] = 0;
	state->m_sh6840_clock_count = 0;
	state->m_sh6840_latchwrite = 0;
	state->m_sh6840_latchwriteold = 0;
	state->m_sh6840_noiselatch1 = 0;
	state->m_sh6840_noiselatch3 = 0;

	/* LFSR */
	state->m_sh6840_LFSR = 0xffffffff;
	state->m_sh6840_LFSR_clocks = 0;
}

static DEVICE_RESET( beezer_sound )
{
	DEVICE_RESET_CALL(common_sh_reset);
}


DEVICE_GET_INFO( beezer_sound )
{
	switch (state)
	{
		/* --- the following bits of info are returned as 64-bit signed integers --- */
		case DEVINFO_INT_TOKEN_BYTES:					info->i = sizeof(beezer_sound_state);			break;

		/* --- the following bits of info are returned as pointers to data or functions --- */
		case DEVINFO_FCT_START:							info->start = DEVICE_START_NAME(beezer_sound);	break;
		case DEVINFO_FCT_RESET:							info->reset = DEVICE_RESET_NAME(beezer_sound);	break;

		/* --- the following bits of info are returned as NULL-terminated strings --- */
		case DEVINFO_STR_NAME:							strcpy(info->s, "beezer SFX");					break;
		case DEVINFO_STR_SOURCE_FILE:						strcpy(info->s, __FILE__);						break;
	}
}

/*************************************
 *
 *  6840 timer handlers
 *
 *************************************/

READ8_DEVICE_HANDLER( beezer_sh6840_r )
{
	beezer_sound_state *state = get_safe_token(device);

	/* force an update of the stream */
	state->m_stream->update();

	switch (offset)
	{
		/* offset 0: Motorola datasheet says it isn't used, Hitachi datasheet says it reads as 0s always*/
		case 0:
		return 0;
		/* offset 1 reads the status register: bits 2 1 0 correspond to ints on channels 2,1,0, and bit 7 is an 'OR' of bits 2,1,0 */
		case 1:
		logerror("%04X:beezer_sh6840_r - unexpected read, status register is TODO!\n", cpu_get_pc(state->m_maincpu));
		return 0;
		/* offsets 2,4,6 read channel 0,1,2 MSBs and latch the LSB*/
		case 2: case 4: case 6:
		state->m_sh6840_LSB_latch = state->m_sh6840_timer[((offset<<1)-1)].counter.b.l;
		return state->m_sh6840_timer[((offset<<1)-1)].counter.b.h;
		/* offsets 3,5,7 read the LSB latch*/
		default: /* case 3,5,7 */
		return state->m_sh6840_LSB_latch;
	}
}

WRITE8_DEVICE_HANDLER( beezer_timer1_w )
{
	beezer_sound_state *state = get_safe_token(device);

	/* force an update of the stream */
	state->m_stream->update();
	state->m_sh6840_latchwriteold = state->m_sh6840_latchwrite;
	state->m_sh6840_latchwrite = data&0x80;
	if ((!state->m_sh6840_latchwriteold) && (state->m_sh6840_latchwrite)) // rising edge
	{
		state->m_sh6840_noiselatch1 = (state->m_sh6840_LFSR&0x1);
	}
}

READ8_DEVICE_HANDLER( beezer_noise_r )
{
	beezer_sound_state *state = get_safe_token(device);

	/* force an update of the stream */
	state->m_stream->update();
	return (state->m_sh6840_LFSR&0x1);
}

WRITE8_DEVICE_HANDLER( beezer_sh6840_w )
{
	beezer_sound_state *state = get_safe_token(device);
	struct sh6840_timer_channel *sh6840_timer = state->m_sh6840_timer;

	/* force an update of the stream */
	state->m_stream->update();

	switch (offset)
	{
		/* offset 0 writes to either channel 0 control or channel 2 control */
		case 0:
			if (sh6840_timer[1].cr & 0x01)
				sh6840_timer[0].cr = data;
			else
				sh6840_timer[2].cr = data;

			/* only support mode 0 and 2 */
			if (((data >> 3) & 5) != 0)
				fprintf(stderr,"beezer_sh6840_w - channel %d configured for mode %d (write was %02x to offset 0)", (sh6840_timer[1].cr & 0x01) ? 0 : 2, (data >> 3) & 7, data);
			break;

		/* offset 1 writes to channel 1 control */
		case 1:
			sh6840_timer[1].cr = data;

			/* only support mode 0 and 2 */
			if (((data >> 3) & 5) != 0)
				fprintf(stderr,"beezer_sh6840_w - channel 1 configured for mode %d (write was %02x to offset 1)", (data >> 3) & 7, data);
			break;

		/* offsets 2/4/6 write to the common MSB latch */
		case 2:
		case 4:
		case 6:
			state->m_sh6840_MSB_latch = data;
			break;

		/* offsets 3/5/7 write to the LSB controls */
		case 3:
		case 5:
		case 7:
		{
			/* latch the timer value */
			int ch = (offset - 3) / 2;
			sh6840_timer[ch].timer = (state->m_sh6840_MSB_latch << 8) | (data & 0xff);

			/* if CR4 is clear, the value is loaded immediately */
			if (!(sh6840_timer[ch].cr & 0x10))
				sh6840_timer[ch].counter.w = sh6840_timer[ch].timer;
			break;
		}
	}
}



/*************************************
 *
 *  DAC write handler
 *
 *************************************/

WRITE8_DEVICE_HANDLER( beezer_sfxctrl_w )
{
	beezer_sound_state *state = get_safe_token(device);
	state->m_stream->update();
	state->m_sh6840_volume[offset] = data;
}

