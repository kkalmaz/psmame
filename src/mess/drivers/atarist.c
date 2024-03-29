#define ADDRESS_MAP_MODERN

#include "emu.h"
#include "cpu/m68000/m68000.h"
#include "cpu/m6800/m6800.h"
#include "audio/lmc1992.h"
#include "formats/atarist_dsk.h"
#include "imagedev/cartslot.h"
#include "imagedev/flopdrv.h"
#include "machine/ram.h"
#include "machine/6850acia.h"
#include "machine/8530scc.h"
#include "machine/ctronics.h"
#include "machine/mc68901.h"
#include "machine/rescap.h"
#include "machine/rp5c15.h"
#include "machine/rs232.h"
#include "machine/wd17xx.h"
#include "sound/ay8910.h"
#include "video/atarist.h"
#include "includes/atarist.h"

/*

    TODO:

    - floppy write
    - floppy DMA transfer timer
    - MSA disk image support
    - mouse moves too fast?
    - UK keyboard layout for the special keys
    - accurate screen timing
    - STe DMA sound and LMC1992 Microwire mixer
    - Mega ST/STe MC68881 FPU
    - MIDI interface
    - Mega STe 16KB cache
    - Mega STe LAN

    http://dev-docs.atariforge.org/
    http://info-coach.fr/atari/software/protection.php

*/



//**************************************************************************
//  CONSTANTS / MACROS
//**************************************************************************

#define LOG 0

static const int IKBD_MOUSE_XYA[3][4] = { { 0, 0, 0, 0 }, { 1, 1, 0, 0 }, { 0, 1, 1, 0 } };
static const int IKBD_MOUSE_XYB[3][4] = { { 0, 0, 0, 0 }, { 0, 1, 1, 0 }, { 1, 1, 0, 0 } };

static const int DMASOUND_RATE[] = { Y2/640/8, Y2/640/4, Y2/640/2, Y2/640 };


//**************************************************************************
//  FLOPPY
//**************************************************************************

//-------------------------------------------------
//  toggle_dma_fifo -
//-------------------------------------------------

void st_state::toggle_dma_fifo()
{
	if (LOG) logerror("Toggling DMA FIFO\n");

	m_fdc_fifo_sel = !m_fdc_fifo_sel;
	m_fdc_fifo_index = 0;
}


//-------------------------------------------------
//  flush_dma_fifo -
//-------------------------------------------------

void st_state::flush_dma_fifo()
{
	address_space *program = m_maincpu->memory().space(AS_PROGRAM);

	if (m_fdc_fifo_empty[m_fdc_fifo_sel]) return;

	for (int i = 0; i < 8; i++)
	{
		UINT16 data = m_fdc_fifo[m_fdc_fifo_sel][i];

		if (LOG) logerror("Flushing DMA FIFO %u data %04x to address %06x\n", m_fdc_fifo_sel, data, m_dma_base);

		program->write_word(m_dma_base, data);
		m_dma_base += 2;
	}

	m_fdc_fifo_empty[m_fdc_fifo_sel] = 1;
}


//-------------------------------------------------
//  fill_dma_fifo -
//-------------------------------------------------

void st_state::fill_dma_fifo()
{
	address_space *program = m_maincpu->memory().space(AS_PROGRAM);

	for (int i = 0; i < 8; i++)
	{
		UINT16 data = program->read_word(m_dma_base);

		if (LOG) logerror("Filling DMA FIFO %u with data %04x from memory address %06x\n", m_fdc_fifo_sel, data, m_dma_base);

		m_fdc_fifo[m_fdc_fifo_sel][i] = data;
		m_dma_base += 2;
	}

	m_fdc_fifo_empty[m_fdc_fifo_sel] = 0;
}


//-------------------------------------------------
//  fdc_dma_transfer -
//-------------------------------------------------

void st_state::fdc_dma_transfer()
{
	if (!m_fdc_dmabytes) return;

	if (m_fdc_mode & DMA_MODE_READ_WRITE)
	{
		UINT16 data = m_fdc_fifo[m_fdc_fifo_sel][m_fdc_fifo_index];

		if (m_fdc_fifo_msb)
		{
			// write LSB to disk
			wd17xx_data_w(m_fdc, 0, data & 0xff);

			if (LOG) logerror("DMA Write to FDC %02x\n", data & 0xff);

			m_fdc_fifo_index++;
		}
		else
		{
			// write MSB to disk
			wd17xx_data_w(m_fdc, 0, data >> 8);

			if (LOG) logerror("DMA Write to FDC %02x\n", data >> 8);
		}

		// toggle MSB/LSB
		m_fdc_fifo_msb = !m_fdc_fifo_msb;

		if (m_fdc_fifo_index == 8)
		{
			m_fdc_fifo_index--;
			m_fdc_fifo_empty[m_fdc_fifo_sel] = 1;

			toggle_dma_fifo();

			if (m_fdc_fifo_empty[m_fdc_fifo_sel])
			{
				fill_dma_fifo();
			}
		}
	}
	else
	{
		// read from controller to FIFO
		UINT8 data = wd17xx_data_r(m_fdc, 0);

		m_fdc_fifo_empty[m_fdc_fifo_sel] = 0;

		if (LOG) logerror("DMA Read from FDC %02x\n", data);

		if (m_fdc_fifo_msb)
		{
			// write MSB to FIFO
			m_fdc_fifo[m_fdc_fifo_sel][m_fdc_fifo_index] |= data;
			m_fdc_fifo_index++;
		}
		else
		{
			// write LSB to FIFO
			m_fdc_fifo[m_fdc_fifo_sel][m_fdc_fifo_index] = data << 8;
		}

		// toggle MSB/LSB
		m_fdc_fifo_msb = !m_fdc_fifo_msb;

		if (m_fdc_fifo_index == 8)
		{
			flush_dma_fifo();
			toggle_dma_fifo();
		}
	}

	m_fdc_dmabytes--;

	if (m_fdc_dmabytes == 0)
	{
		m_fdc_sectors--;

		if (m_fdc_sectors)
		{
			m_fdc_dmabytes = DMA_SECTOR_SIZE;
		}
	}
}


//-------------------------------------------------
//  fdc_data_r -
//-------------------------------------------------

READ16_MEMBER( st_state::fdc_data_r )
{
	UINT8 data = 0;

	if (m_fdc_mode & DMA_MODE_SECTOR_COUNT)
	{
		if (LOG) logerror("Indeterminate DMA Sector Count Read!\n");

		// sector count register is write only, reading it returns unpredictable values
		data = machine().rand() & 0xff;
	}
	else
	{
		if (!(m_fdc_mode & DMA_MODE_FDC_HDC_CS))
		{
			// floppy controller
			int offset = (m_fdc_mode & DMA_MODE_ADDRESS_MASK) >> 1;

			data = wd17xx_r(m_fdc, offset);

			if (LOG) logerror("FDC Register %u Read %02x\n", offset, data);
		}
	}

	return data;
}


//-------------------------------------------------
//  fdc_data_w -
//-------------------------------------------------

WRITE16_MEMBER( st_state::fdc_data_w )
{
	if (m_fdc_mode & DMA_MODE_SECTOR_COUNT)
	{
		if (LOG) logerror("DMA Sector Count %u\n", data);

		// sector count register
		m_fdc_sectors = data;

		if (m_fdc_sectors)
		{
			m_fdc_dmabytes = DMA_SECTOR_SIZE;
		}

		if (m_fdc_mode & DMA_MODE_READ_WRITE)
		{
			// fill both FIFOs with data
			fill_dma_fifo();
			toggle_dma_fifo();
			fill_dma_fifo();
			toggle_dma_fifo();
		}
	}
	else
	{
		if (!(m_fdc_mode & DMA_MODE_FDC_HDC_CS))
		{
			// floppy controller
			int offset = (m_fdc_mode & DMA_MODE_ADDRESS_MASK) >> 1;

			if (LOG) logerror("FDC Register %u Write %02x\n", offset, data);

			wd17xx_w(m_fdc, offset, data);
		}
	}
}


//-------------------------------------------------
//  dma_status_r -
//-------------------------------------------------

READ16_MEMBER( st_state::dma_status_r )
{
	UINT16 data = 0;

	// DMA error
	data |= m_dma_error;

	// sector count null
	data |= !(m_fdc_sectors == 0) << 1;

	// DRQ state
	data |= wd17xx_drq_r(m_fdc) << 2;

	return data;
}


//-------------------------------------------------
//  dma_mode_w -
//-------------------------------------------------

WRITE16_MEMBER( st_state::dma_mode_w )
{
	if (LOG) logerror("DMA Mode %04x\n", data);

	if ((data & DMA_MODE_READ_WRITE) != (m_fdc_mode & DMA_MODE_READ_WRITE))
	{
		if (LOG) logerror("DMA reset\n");

		flush_dma_fifo();

		m_dma_error = 1;
		m_fdc_sectors = 0;
		m_fdc_fifo_sel = 0;
		m_fdc_fifo_msb = 0;
	}

	m_fdc_mode = data;
}


//-------------------------------------------------
//  dma_counter_r -
//-------------------------------------------------

READ8_MEMBER( st_state::dma_counter_r )
{
	UINT8 data = 0;

	switch (offset)
	{
	case 0:
		data = (m_dma_base >> 16) & 0xff;

	case 1:
		data = (m_dma_base >> 8) & 0xff;

	case 2:
		data = m_dma_base & 0xff;
	}

	return data;
}


//-------------------------------------------------
//  dma_base_w -
//-------------------------------------------------

WRITE8_MEMBER( st_state::dma_base_w )
{
	switch (offset)
	{
	case 0:
		m_dma_base = (m_dma_base & 0x00ffff) | (data << 16);
		if (LOG) logerror("DMA Address High %02x (%06x)\n", data & 0xff, m_dma_base);
		break;

	case 1:
		m_dma_base = (m_dma_base & 0x0000ff) | (data << 8);
		if (LOG) logerror("DMA Address Mid %02x (%06x)\n", data & 0xff, m_dma_base);
		break;

	case 2:
		m_dma_base = data & 0xff;
		if (LOG) logerror("DMA Address Low %02x (%06x)\n", data & 0xff, m_dma_base);
		break;
	}
}



//**************************************************************************
//  MMU
//**************************************************************************

//-------------------------------------------------
//  mmu_r -
//-------------------------------------------------

READ8_MEMBER( st_state::mmu_r )
{
	return m_mmu;
}


//-------------------------------------------------
//  mmu_w -
//-------------------------------------------------

WRITE8_MEMBER( st_state::mmu_w )
{
	if (LOG) logerror("Memory Configuration Register: %02x\n", data);

	m_mmu = data;
}



//**************************************************************************
//  IKBD
//**************************************************************************

//-------------------------------------------------
//  mouse_tick -
//-------------------------------------------------

void st_state::mouse_tick()
{
	/*

            Right   Left        Up      Down

        XA  1100    0110    YA  1100    0110
        XB  0110    1100    YB  0110    1100

    */

	UINT8 x = input_port_read_safe(machine(), "IKBD_MOUSEX", 0x00);
	UINT8 y = input_port_read_safe(machine(), "IKBD_MOUSEY", 0x00);

	if (m_ikbd_mouse_pc == 0)
	{
		if (x == m_ikbd_mouse_x)
		{
			m_ikbd_mouse_px = IKBD_MOUSE_PHASE_STATIC;
		}
		else if ((x > m_ikbd_mouse_x) || (x == 0 && m_ikbd_mouse_x == 0xff))
		{
			m_ikbd_mouse_px = IKBD_MOUSE_PHASE_POSITIVE;
		}
		else if ((x < m_ikbd_mouse_x) || (x == 0xff && m_ikbd_mouse_x == 0))
		{
			m_ikbd_mouse_px = IKBD_MOUSE_PHASE_NEGATIVE;
		}

		if (y == m_ikbd_mouse_y)
		{
			m_ikbd_mouse_py = IKBD_MOUSE_PHASE_STATIC;
		}
		else if ((y > m_ikbd_mouse_y) || (y == 0 && m_ikbd_mouse_y == 0xff))
		{
			m_ikbd_mouse_py = IKBD_MOUSE_PHASE_POSITIVE;
		}
		else if ((y < m_ikbd_mouse_y) || (y == 0xff && m_ikbd_mouse_y == 0))
		{
			m_ikbd_mouse_py = IKBD_MOUSE_PHASE_NEGATIVE;
		}

		m_ikbd_mouse_x = x;
		m_ikbd_mouse_y = y;
	}

	m_ikbd_mouse = 0;

	m_ikbd_mouse |= IKBD_MOUSE_XYB[m_ikbd_mouse_px][m_ikbd_mouse_pc];	   // XB
	m_ikbd_mouse |= IKBD_MOUSE_XYA[m_ikbd_mouse_px][m_ikbd_mouse_pc] << 1; // XA
	m_ikbd_mouse |= IKBD_MOUSE_XYB[m_ikbd_mouse_py][m_ikbd_mouse_pc] << 2; // YA
	m_ikbd_mouse |= IKBD_MOUSE_XYA[m_ikbd_mouse_py][m_ikbd_mouse_pc] << 3; // YB

	m_ikbd_mouse_pc++;
	m_ikbd_mouse_pc &= 0x03;
}


//-------------------------------------------------
//  TIMER_CALLBACK( st_mouse_tick )
//-------------------------------------------------

static TIMER_CALLBACK( st_mouse_tick )
{
	st_state *state = machine.driver_data<st_state>();

	state->mouse_tick();
}


//-------------------------------------------------
//  ikbd_port1_r -
//-------------------------------------------------

READ8_MEMBER( st_state::ikbd_port1_r )
{
	/*

        bit     description

        0       Keyboard column input
        1       Keyboard column input
        2       Keyboard column input
        3       Keyboard column input
        4       Keyboard column input
        5       Keyboard column input
        6       Keyboard column input
        7       Keyboard column input

    */

	UINT8 data = 0xff;

	// keyboard data
	if (!BIT(m_ikbd_keylatch, 1)) data &= input_port_read(machine(), "P31");
	if (!BIT(m_ikbd_keylatch, 2)) data &= input_port_read(machine(), "P32");
	if (!BIT(m_ikbd_keylatch, 3)) data &= input_port_read(machine(), "P33");
	if (!BIT(m_ikbd_keylatch, 4)) data &= input_port_read(machine(), "P34");
	if (!BIT(m_ikbd_keylatch, 5)) data &= input_port_read(machine(), "P35");
	if (!BIT(m_ikbd_keylatch, 6)) data &= input_port_read(machine(), "P36");
	if (!BIT(m_ikbd_keylatch, 7)) data &= input_port_read(machine(), "P37");
	if (!BIT(m_ikbd_keylatch, 8)) data &= input_port_read(machine(), "P40");
	if (!BIT(m_ikbd_keylatch, 9)) data &= input_port_read(machine(), "P41");
	if (!BIT(m_ikbd_keylatch, 10)) data &= input_port_read(machine(), "P42");
	if (!BIT(m_ikbd_keylatch, 11)) data &= input_port_read(machine(), "P43");
	if (!BIT(m_ikbd_keylatch, 12)) data &= input_port_read(machine(), "P44");
	if (!BIT(m_ikbd_keylatch, 13)) data &= input_port_read(machine(), "P45");
	if (!BIT(m_ikbd_keylatch, 14)) data &= input_port_read(machine(), "P46");
	if (!BIT(m_ikbd_keylatch, 15)) data &= input_port_read(machine(), "P47");

	return data;
}


//-------------------------------------------------
//  ikbd_port2_r -
//-------------------------------------------------

READ8_MEMBER( st_state::ikbd_port2_r )
{
	/*

        bit     description

        0       JOY 1-5
        1       JOY 0-6
        2       JOY 1-6
        3       SD FROM CPU
        4

    */

	UINT8 data = input_port_read_safe(machine(), "IKBD_JOY1", 0xff) & 0x06;

	// serial receive
	data |= m_ikbd_tx << 3;

	return data;
}


//-------------------------------------------------
//  ikbd_port2_w -
//-------------------------------------------------

WRITE8_MEMBER( st_state::ikbd_port2_w )
{
	/*

        bit     description

        0       joystick enable
        1
        2
        3
        4       SD TO CPU

    */

	// joystick enable
	m_ikbd_joy = BIT(data, 0);

	// serial transmit
	m_ikbd_rx = BIT(data, 4);
}


//-------------------------------------------------
//  ikbd_port3_w -
//-------------------------------------------------

WRITE8_MEMBER( st_state::ikbd_port3_w )
{
	/*

        bit     description

        0       CAPS LOCK LED
        1       Keyboard row select
        2       Keyboard row select
        3       Keyboard row select
        4       Keyboard row select
        5       Keyboard row select
        6       Keyboard row select
        7       Keyboard row select

    */

	// caps lock led
	set_led_status(machine(), 1, BIT(data, 0));

	// keyboard row select
	m_ikbd_keylatch = (m_ikbd_keylatch & 0xff00) | data;
}


//-------------------------------------------------
//  ikbd_port4_r -
//-------------------------------------------------

READ8_MEMBER( st_state::ikbd_port4_r )
{
	/*

        bit     description

        0       JOY 0-1 or mouse XB
        1       JOY 0-2 or mouse XA
        2       JOY 0-3 or mouse YA
        3       JOY 0-4 or mouse YB
        4       JOY 1-1
        5       JOY 1-2
        6       JOY 1-3
        7       JOY 1-4

    */

	if (m_ikbd_joy) return 0xff;

	UINT8 data = input_port_read_safe(machine(), "IKBD_JOY0", 0xff);

	if ((input_port_read(machine(), "config") & 0x01) == 0)
	{
		data = (data & 0xf0) | m_ikbd_mouse;
	}

	return data;
}


//-------------------------------------------------
//  ikbd_port4_w -
//-------------------------------------------------

WRITE8_MEMBER( st_state::ikbd_port4_w )
{
	/*

        bit     description

        0       Keyboard row select
        1       Keyboard row select
        2       Keyboard row select
        3       Keyboard row select
        4       Keyboard row select
        5       Keyboard row select
        6       Keyboard row select
        7       Keyboard row select

    */

	// keyboard row select
	m_ikbd_keylatch = (data << 8) | (m_ikbd_keylatch & 0xff);
}



//**************************************************************************
//  FPU
//**************************************************************************

//-------------------------------------------------
//  fpu_r -
//-------------------------------------------------

READ16_MEMBER( megast_state::fpu_r )
{
	// HACK diagnostic cartridge wants to see this value
	return 0x0802;
}


WRITE16_MEMBER( megast_state::fpu_w )
{
}



//**************************************************************************
//  DMA SOUND
//**************************************************************************

//-------------------------------------------------
//  dmasound_set_state -
//-------------------------------------------------

void ste_state::dmasound_set_state(int level)
{
	m_dmasnd_active = level;
	m_mfp->tai_w(level);

	if (level == 0)
	{
		m_dmasnd_baselatch = m_dmasnd_base;
		m_dmasnd_endlatch = m_dmasnd_end;
	}
	else
	{
		m_dmasnd_cntr = m_dmasnd_baselatch;
	}
}


//-------------------------------------------------
//  dmasound_tick -
//-------------------------------------------------

void ste_state::dmasound_tick()
{
	if (m_dmasnd_samples == 0)
	{
		UINT8 *RAM = ram_get_ptr(machine().device(RAM_TAG));

		for (int i = 0; i < 8; i++)
		{
			m_dmasnd_fifo[i] = RAM[m_dmasnd_cntr];
			m_dmasnd_cntr++;
			m_dmasnd_samples++;

			if (m_dmasnd_cntr == m_dmasnd_endlatch)
			{
				dmasound_set_state(0);
				break;
			}
		}
	}

	if (m_dmasnd_ctrl & 0x80)
	{
		if (LOG) logerror("DMA sound left  %i\n", m_dmasnd_fifo[7 - m_dmasnd_samples]);
		m_dmasnd_samples--;

		if (LOG) logerror("DMA sound right %i\n", m_dmasnd_fifo[7 - m_dmasnd_samples]);
		m_dmasnd_samples--;
	}
	else
	{
		if (LOG) logerror("DMA sound mono %i\n", m_dmasnd_fifo[7 - m_dmasnd_samples]);
		m_dmasnd_samples--;
	}

	if ((m_dmasnd_samples == 0) && (m_dmasnd_active == 0))
	{
		if ((m_dmasnd_ctrl & 0x03) == 0x03)
		{
			dmasound_set_state(1);
		}
		else
		{
			m_dmasound_timer->enable(0);
		}
	}
}


//-------------------------------------------------
//  TIMER_CALLBACK( atariste_dmasound_tick )
//-------------------------------------------------

static TIMER_CALLBACK( atariste_dmasound_tick )
{
	ste_state *state = machine.driver_data<ste_state>();

	state->dmasound_tick();
}


//-------------------------------------------------
//  sound_dma_control_r -
//-------------------------------------------------

READ8_MEMBER( ste_state::sound_dma_control_r )
{
	return m_dmasnd_ctrl;
}


//-------------------------------------------------
//  sound_dma_base_r -
//-------------------------------------------------

READ8_MEMBER( ste_state::sound_dma_base_r )
{
	UINT8 data = 0;

	switch (offset)
	{
	case 0x00:
		data = (m_dmasnd_base >> 16) & 0x3f;
		break;

	case 0x01:
		data = (m_dmasnd_base >> 8) & 0xff;
		break;

	case 0x02:
		data = m_dmasnd_base & 0xff;
		break;
	}

	return data;
}


//-------------------------------------------------
//  sound_dma_counter_r -
//-------------------------------------------------

READ8_MEMBER( ste_state::sound_dma_counter_r )
{
	UINT8 data = 0;

	switch (offset)
	{
	case 0x00:
		data = (m_dmasnd_cntr >> 16) & 0x3f;
		break;

	case 0x01:
		data = (m_dmasnd_cntr >> 8) & 0xff;
		break;

	case 0x02:
		data = m_dmasnd_cntr & 0xff;
		break;
	}

	return data;
}


//-------------------------------------------------
//  sound_dma_end_r -
//-------------------------------------------------

READ8_MEMBER( ste_state::sound_dma_end_r )
{
	UINT8 data = 0;

	switch (offset)
	{
	case 0x00:
		data = (m_dmasnd_end >> 16) & 0x3f;
		break;

	case 0x01:
		data = (m_dmasnd_end >> 8) & 0xff;
		break;

	case 0x02:
		data = m_dmasnd_end & 0xff;
		break;
	}

	return data;
}


//-------------------------------------------------
//  sound_mode_r -
//-------------------------------------------------

READ8_MEMBER( ste_state::sound_mode_r )
{
	return m_dmasnd_mode;
}


//-------------------------------------------------
//  sound_dma_control_w -
//-------------------------------------------------

WRITE8_MEMBER( ste_state::sound_dma_control_w )
{
	m_dmasnd_ctrl = data & 0x03;

	if (m_dmasnd_ctrl & 0x01)
	{
		if (!m_dmasnd_active)
		{
			dmasound_set_state(1);
			m_dmasound_timer->adjust(attotime::zero, 0, attotime::from_hz(DMASOUND_RATE[m_dmasnd_mode & 0x03]));
		}
	}
	else
	{
		dmasound_set_state(0);
		m_dmasound_timer->enable(0);
	}
}


//-------------------------------------------------
//  sound_dma_base_w -
//-------------------------------------------------

WRITE8_MEMBER( ste_state::sound_dma_base_w )
{
	switch (offset)
	{
	case 0x00:
		m_dmasnd_base = (data << 16) & 0x3f0000;
		break;
	case 0x01:
		m_dmasnd_base = (m_dmasnd_base & 0x3f00fe) | (data << 8);
		break;
	case 0x02:
		m_dmasnd_base = (m_dmasnd_base & 0x3fff00) | (data & 0xfe);
		break;
	}

	if (!m_dmasnd_active)
	{
		m_dmasnd_baselatch = m_dmasnd_base;
	}
}


//-------------------------------------------------
//  sound_dma_end_w -
//-------------------------------------------------

WRITE8_MEMBER( ste_state::sound_dma_end_w )
{
	switch (offset)
	{
	case 0x00:
		m_dmasnd_end = (data << 16) & 0x3f0000;
		break;
	case 0x01:
		m_dmasnd_end = (m_dmasnd_end & 0x3f00fe) | (data & 0xff) << 8;
		break;
	case 0x02:
		m_dmasnd_end = (m_dmasnd_end & 0x3fff00) | (data & 0xfe);
		break;
	}

	if (!m_dmasnd_active)
	{
		m_dmasnd_endlatch = m_dmasnd_end;
	}
}


//-------------------------------------------------
//  sound_mode_w -
//-------------------------------------------------

WRITE8_MEMBER( ste_state::sound_mode_w )
{
	m_dmasnd_mode = data & 0x83;
}



//**************************************************************************
//  MICROWIRE
//**************************************************************************

//-------------------------------------------------
//  microwire_shift -
//-------------------------------------------------

void ste_state::microwire_shift()
{
	if (BIT(m_mw_mask, 15))
	{
		lmc1992_data_w(m_lmc1992, BIT(m_mw_data, 15));
		lmc1992_clock_w(m_lmc1992, 1);
		lmc1992_clock_w(m_lmc1992, 0);
	}

	// rotate mask and data left
	m_mw_mask = (m_mw_mask << 1) | BIT(m_mw_mask, 15);
	m_mw_data = (m_mw_data << 1) | BIT(m_mw_data, 15);
	m_mw_shift++;
}


//-------------------------------------------------
//  microwire_tick -
//-------------------------------------------------

void ste_state::microwire_tick()
{
	switch (m_mw_shift)
	{
	case 0:
		lmc1992_enable_w(m_lmc1992, 0);
		microwire_shift();
		break;

	default:
		microwire_shift();
		break;

	case 15:
		microwire_shift();
		lmc1992_enable_w(m_lmc1992, 1);
		m_mw_shift = 0;
		m_microwire_timer->enable(0);
		break;
	}
}


//-------------------------------------------------
//  TIMER_CALLBACK( atariste_microwire_tick )
//-------------------------------------------------

static TIMER_CALLBACK( atariste_microwire_tick )
{
	ste_state *state = machine.driver_data<ste_state>();

	state->microwire_tick();
}


//-------------------------------------------------
//  microwire_data_r -
//-------------------------------------------------

READ16_MEMBER( ste_state::microwire_data_r )
{
	return m_mw_data;
}


//-------------------------------------------------
//  microwire_data_w -
//-------------------------------------------------

WRITE16_MEMBER( ste_state::microwire_data_w )
{
	if (!m_microwire_timer->enabled())
	{
		m_mw_data = data;
		m_microwire_timer->adjust(attotime::zero, 0, attotime::from_usec(2));
	}
}


//-------------------------------------------------
//  microwire_mask_r -
//-------------------------------------------------

READ16_MEMBER( ste_state::microwire_mask_r )
{
	return m_mw_mask;
}


//-------------------------------------------------
//  microwire_mask_w -
//-------------------------------------------------

WRITE16_MEMBER( ste_state::microwire_mask_w )
{
	if (!m_microwire_timer->enabled())
	{
		m_mw_mask = data;
	}
}



//**************************************************************************
//  CACHE
//**************************************************************************

//-------------------------------------------------
//  cache_r -
//-------------------------------------------------

READ16_MEMBER( megaste_state::cache_r )
{
	return m_cache;
}


//-------------------------------------------------
//  cache_w -
//-------------------------------------------------

WRITE16_MEMBER( megaste_state::cache_w )
{
	m_cache = data;

	m_maincpu->set_unscaled_clock(BIT(data, 0) ? Y2/2 : Y2/4);
}



//**************************************************************************
//  STBOOK
//**************************************************************************

//-------------------------------------------------
//  config_r -
//-------------------------------------------------

READ16_MEMBER( stbook_state::config_r )
{
	/*

        bit     description

        0       _POWER_SWITCH
        1       _TOP_CLOSED
        2       _RTC_ALARM
        3       _SOURCE_DEAD
        4       _SOURCE_LOW
        5       _MODEM_WAKE
        6       (reserved)
        7       _EXPANSION_WAKE
        8       (reserved)
        9       (reserved)
        10      (reserved)
        11      (reserved)
        12      (reserved)
        13      SELF TEST
        14      LOW SPEED FLOPPY
        15      DMA AVAILABLE

    */

	return (input_port_read(machine(), "SW400") << 8) | 0xff;
}


//-------------------------------------------------
//  lcd_control_w -
//-------------------------------------------------

WRITE16_MEMBER( stbook_state::lcd_control_w )
{
	/*

        bit     description

        0       Shadow Chip OFF
        1       _SHIFTER OFF
        2       POWEROFF
        3       _22ON
        4       RS-232_OFF
        5       (reserved)
        6       (reserved)
        7       MTR_PWR_ON

    */
}



//**************************************************************************
//  ADDRESS MAPS
//**************************************************************************

//-------------------------------------------------
//  ADDRESS_MAP( ikbd_map )
//-------------------------------------------------

static ADDRESS_MAP_START( ikbd_map, AS_PROGRAM, 8, st_state )
	AM_RANGE(0x0000, 0x001f) AM_READWRITE_LEGACY(m6801_io_r, m6801_io_w)
	AM_RANGE(0x0080, 0x00ff) AM_RAM
	AM_RANGE(0xf000, 0xffff) AM_ROM AM_REGION(HD6301V1_TAG, 0)
ADDRESS_MAP_END


//-------------------------------------------------
//  ADDRESS_MAP( ikbd_io_map )
//-------------------------------------------------

static ADDRESS_MAP_START( ikbd_io_map, AS_IO, 8, st_state )
	AM_RANGE(M6801_PORT1, M6801_PORT1) AM_READ(ikbd_port1_r)
	AM_RANGE(M6801_PORT2, M6801_PORT2) AM_READWRITE(ikbd_port2_r, ikbd_port2_w)
	AM_RANGE(M6801_PORT3, M6801_PORT3) AM_WRITE(ikbd_port3_w)
	AM_RANGE(M6801_PORT4, M6801_PORT4) AM_READWRITE(ikbd_port4_r, ikbd_port4_w)
ADDRESS_MAP_END


//-------------------------------------------------
//  ADDRESS_MAP( st_map )
//-------------------------------------------------

static ADDRESS_MAP_START( st_map, AS_PROGRAM, 16, st_state )
	ADDRESS_MAP_UNMAP_HIGH
	AM_RANGE(0x000000, 0x000007) AM_ROM AM_REGION(M68000_TAG, 0)
	AM_RANGE(0x000008, 0x1fffff) AM_RAM
	AM_RANGE(0x200000, 0x3fffff) AM_RAM
	AM_RANGE(0xfa0000, 0xfbffff) AM_ROM AM_REGION("cart", 0)
	AM_RANGE(0xfc0000, 0xfeffff) AM_ROM AM_REGION(M68000_TAG, 0)
	AM_RANGE(0xff8000, 0xff8001) AM_READWRITE8(mmu_r, mmu_w, 0x00ff)
	AM_RANGE(0xff8200, 0xff8203) AM_READWRITE8(shifter_base_r, shifter_base_w, 0x00ff)
	AM_RANGE(0xff8204, 0xff8209) AM_READ8(shifter_counter_r, 0x00ff)
	AM_RANGE(0xff820a, 0xff820b) AM_READWRITE8(shifter_sync_r, shifter_sync_w, 0xff00)
	AM_RANGE(0xff8240, 0xff825f) AM_READWRITE(shifter_palette_r, shifter_palette_w)
	AM_RANGE(0xff8260, 0xff8261) AM_READWRITE8(shifter_mode_r, shifter_mode_w, 0xff00)
	AM_RANGE(0xff8604, 0xff8605) AM_READWRITE(fdc_data_r, fdc_data_w)
	AM_RANGE(0xff8606, 0xff8607) AM_READWRITE(dma_status_r, dma_mode_w)
	AM_RANGE(0xff8608, 0xff860d) AM_READWRITE8(dma_counter_r, dma_base_w, 0x00ff)
	AM_RANGE(0xff8800, 0xff8801) AM_DEVREADWRITE8_LEGACY(YM2149_TAG, ay8910_r, ay8910_data_w, 0xff00)
	AM_RANGE(0xff8802, 0xff8803) AM_DEVWRITE8_LEGACY(YM2149_TAG, ay8910_data_w, 0xff00)
	AM_RANGE(0xff8a00, 0xff8a1f) AM_READWRITE(blitter_halftone_r, blitter_halftone_w)
	AM_RANGE(0xff8a20, 0xff8a21) AM_READWRITE(blitter_src_inc_x_r, blitter_src_inc_x_w)
	AM_RANGE(0xff8a22, 0xff8a23) AM_READWRITE(blitter_src_inc_y_r, blitter_src_inc_y_w)
	AM_RANGE(0xff8a24, 0xff8a27) AM_READWRITE(blitter_src_r, blitter_src_w)
	AM_RANGE(0xff8a28, 0xff8a2d) AM_READWRITE(blitter_end_mask_r, blitter_end_mask_w)
	AM_RANGE(0xff8a2e, 0xff8a2f) AM_READWRITE(blitter_dst_inc_x_r, blitter_dst_inc_x_w)
	AM_RANGE(0xff8a30, 0xff8a31) AM_READWRITE(blitter_dst_inc_y_r, blitter_dst_inc_y_w)
	AM_RANGE(0xff8a32, 0xff8a35) AM_READWRITE(blitter_dst_r, blitter_dst_w)
	AM_RANGE(0xff8a36, 0xff8a37) AM_READWRITE(blitter_count_x_r, blitter_count_x_w)
	AM_RANGE(0xff8a38, 0xff8a39) AM_READWRITE(blitter_count_y_r, blitter_count_y_w)
	AM_RANGE(0xff8a3a, 0xff8a3b) AM_READWRITE(blitter_op_r, blitter_op_w)
	AM_RANGE(0xff8a3c, 0xff8a3d) AM_READWRITE(blitter_ctrl_r, blitter_ctrl_w)
	AM_RANGE(0xfffa00, 0xfffa3f) AM_DEVREADWRITE8(MC68901_TAG, mc68901_device, read, write, 0x00ff)
	AM_RANGE(0xfffc00, 0xfffc01) AM_DEVREADWRITE8_LEGACY(MC6850_0_TAG, acia6850_stat_r, acia6850_ctrl_w, 0xff00)
	AM_RANGE(0xfffc02, 0xfffc03) AM_DEVREADWRITE8_LEGACY(MC6850_0_TAG, acia6850_data_r, acia6850_data_w, 0xff00)
	AM_RANGE(0xfffc04, 0xfffc05) AM_DEVREADWRITE8_LEGACY(MC6850_1_TAG, acia6850_stat_r, acia6850_ctrl_w, 0xff00)
	AM_RANGE(0xfffc06, 0xfffc07) AM_DEVREADWRITE8_LEGACY(MC6850_1_TAG, acia6850_data_r, acia6850_data_w, 0xff00)
ADDRESS_MAP_END


//-------------------------------------------------
//  ADDRESS_MAP( megast_map )
//-------------------------------------------------

static ADDRESS_MAP_START( megast_map, AS_PROGRAM, 16, megast_state )
	ADDRESS_MAP_UNMAP_HIGH
	AM_RANGE(0x000000, 0x000007) AM_ROM AM_REGION(M68000_TAG, 0)
	AM_RANGE(0x000008, 0x1fffff) AM_RAM
	AM_RANGE(0x200000, 0x3fffff) AM_RAM
	AM_RANGE(0xfa0000, 0xfbffff) AM_ROM AM_REGION("cart", 0)
	AM_RANGE(0xfc0000, 0xfeffff) AM_ROM AM_REGION(M68000_TAG, 0)
//  AM_RANGE(0xff7f30, 0xff7f31) AM_READWRITE_BASE(st_state, blitter_dst_inc_y_r, blitter_dst_inc_y_w) // for TOS 1.02
	AM_RANGE(0xff8000, 0xff8001) AM_READWRITE8_BASE(st_state, mmu_r, mmu_w, 0x00ff)
	AM_RANGE(0xff8200, 0xff8203) AM_READWRITE8_BASE(st_state, shifter_base_r, shifter_base_w, 0x00ff)
	AM_RANGE(0xff8204, 0xff8209) AM_READ8_BASE(st_state, shifter_counter_r, 0x00ff)
	AM_RANGE(0xff820a, 0xff820b) AM_READWRITE8_BASE(st_state, shifter_sync_r, shifter_sync_w, 0xff00)
	AM_RANGE(0xff8240, 0xff825f) AM_READWRITE_BASE(st_state, shifter_palette_r, shifter_palette_w)
	AM_RANGE(0xff8260, 0xff8261) AM_READWRITE8_BASE(st_state, shifter_mode_r, shifter_mode_w, 0xff00)
	AM_RANGE(0xff8604, 0xff8605) AM_READWRITE_BASE(st_state, fdc_data_r, fdc_data_w)
	AM_RANGE(0xff8606, 0xff8607) AM_READWRITE_BASE(st_state, dma_status_r, dma_mode_w)
	AM_RANGE(0xff8608, 0xff860d) AM_READWRITE8_BASE(st_state, dma_counter_r, dma_base_w, 0x00ff)
	AM_RANGE(0xff8800, 0xff8801) AM_DEVREADWRITE8_LEGACY(YM2149_TAG, ay8910_r, ay8910_data_w, 0xff00)
	AM_RANGE(0xff8802, 0xff8803) AM_DEVWRITE8_LEGACY(YM2149_TAG, ay8910_data_w, 0xff00)
	AM_RANGE(0xff8a00, 0xff8a1f) AM_READWRITE_BASE(st_state, blitter_halftone_r, blitter_halftone_w)
	AM_RANGE(0xff8a20, 0xff8a21) AM_READWRITE_BASE(st_state, blitter_src_inc_x_r, blitter_src_inc_x_w)
	AM_RANGE(0xff8a22, 0xff8a23) AM_READWRITE_BASE(st_state, blitter_src_inc_y_r, blitter_src_inc_y_w)
	AM_RANGE(0xff8a24, 0xff8a27) AM_READWRITE_BASE(st_state, blitter_src_r, blitter_src_w)
	AM_RANGE(0xff8a28, 0xff8a2d) AM_READWRITE_BASE(st_state, blitter_end_mask_r, blitter_end_mask_w)
	AM_RANGE(0xff8a2e, 0xff8a2f) AM_READWRITE_BASE(st_state, blitter_dst_inc_x_r, blitter_dst_inc_x_w)
	AM_RANGE(0xff8a30, 0xff8a31) AM_READWRITE_BASE(st_state, blitter_dst_inc_y_r, blitter_dst_inc_y_w)
	AM_RANGE(0xff8a32, 0xff8a35) AM_READWRITE_BASE(st_state, blitter_dst_r, blitter_dst_w)
	AM_RANGE(0xff8a36, 0xff8a37) AM_READWRITE_BASE(st_state, blitter_count_x_r, blitter_count_x_w)
	AM_RANGE(0xff8a38, 0xff8a39) AM_READWRITE_BASE(st_state, blitter_count_y_r, blitter_count_y_w)
	AM_RANGE(0xff8a3a, 0xff8a3b) AM_READWRITE_BASE(st_state, blitter_op_r, blitter_op_w)
	AM_RANGE(0xff8a3c, 0xff8a3d) AM_READWRITE_BASE(st_state, blitter_ctrl_r, blitter_ctrl_w)
	AM_RANGE(0xfffa00, 0xfffa3f) AM_DEVREADWRITE8(MC68901_TAG, mc68901_device, read, write, 0x00ff)
	AM_RANGE(0xfffa40, 0xfffa57) AM_READWRITE(fpu_r, fpu_w)
	AM_RANGE(0xfffc00, 0xfffc01) AM_DEVREADWRITE8_LEGACY(MC6850_0_TAG, acia6850_stat_r, acia6850_ctrl_w, 0xff00)
	AM_RANGE(0xfffc02, 0xfffc03) AM_DEVREADWRITE8_LEGACY(MC6850_0_TAG, acia6850_data_r, acia6850_data_w, 0xff00)
	AM_RANGE(0xfffc04, 0xfffc05) AM_DEVREADWRITE8_LEGACY(MC6850_1_TAG, acia6850_stat_r, acia6850_ctrl_w, 0xff00)
	AM_RANGE(0xfffc06, 0xfffc07) AM_DEVREADWRITE8_LEGACY(MC6850_1_TAG, acia6850_data_r, acia6850_data_w, 0xff00)
	AM_RANGE(0xfffc20, 0xfffc3f) AM_DEVREADWRITE_LEGACY(RP5C15_TAG, rp5c15_r, rp5c15_w)
ADDRESS_MAP_END


//-------------------------------------------------
//  ADDRESS_MAP( ste_map )
//-------------------------------------------------

static ADDRESS_MAP_START( ste_map, AS_PROGRAM, 16, ste_state )
	AM_IMPORT_FROM(st_map)
/*  AM_RANGE(0xe00000, 0xe3ffff) AM_ROM AM_REGION(M68000_TAG, 0)
    AM_RANGE(0xff8204, 0xff8209) AM_READWRITE8(shifter_counter_r, shifter_counter_w, 0x00ff)
    AM_RANGE(0xff820c, 0xff820d) AM_READWRITE8(shifter_base_low_r, shifter_base_low_w, 0x00ff)
    AM_RANGE(0xff820e, 0xff820f) AM_READWRITE8(shifter_lineofs_r, shifter_lineofs_w, 0x00ff)
    AM_RANGE(0xff8264, 0xff8265) AM_READWRITE8(shifter_pixelofs_r, shifter_pixelofs_w, 0xffff)
    AM_RANGE(0xff8900, 0xff8901) AM_READWRITE8(sound_dma_control_r, sound_dma_control_w, 0x00ff)
    AM_RANGE(0xff8902, 0xff8907) AM_READWRITE8(sound_dma_base_r, sound_dma_base_w, 0x00ff)
    AM_RANGE(0xff8908, 0xff890d) AM_READ8(sound_dma_counter_r, 0x00ff)
    AM_RANGE(0xff890e, 0xff8913) AM_READWRITE8(sound_dma_end_r, sound_dma_end_w, 0x00ff)
    AM_RANGE(0xff8920, 0xff8921) AM_READWRITE8(sound_mode_r, sound_mode_w, 0x00ff)
    AM_RANGE(0xff8922, 0xff8923) AM_READWRITE(microwire_data_r, microwire_data_w)
    AM_RANGE(0xff8924, 0xff8925) AM_READWRITE(microwire_mask_r, microwire_mask_w)
    AM_RANGE(0xff8a00, 0xff8a1f) AM_READWRITE(blitter_halftone_r, blitter_halftone_w)
    AM_RANGE(0xff8a20, 0xff8a21) AM_READWRITE(blitter_src_inc_x_r, blitter_src_inc_x_w)
    AM_RANGE(0xff8a22, 0xff8a23) AM_READWRITE(blitter_src_inc_y_r, blitter_src_inc_y_w)
    AM_RANGE(0xff8a24, 0xff8a27) AM_READWRITE(blitter_src_r, blitter_src_w)
    AM_RANGE(0xff8a28, 0xff8a2d) AM_READWRITE(blitter_end_mask_r, blitter_end_mask_w)
    AM_RANGE(0xff8a2e, 0xff8a2f) AM_READWRITE(blitter_dst_inc_x_r, blitter_dst_inc_x_w)
    AM_RANGE(0xff8a30, 0xff8a31) AM_READWRITE(blitter_dst_inc_y_r, blitter_dst_inc_y_w)
    AM_RANGE(0xff8a32, 0xff8a35) AM_READWRITE(blitter_dst_r, blitter_dst_w)
    AM_RANGE(0xff8a36, 0xff8a37) AM_READWRITE(blitter_count_x_r, blitter_count_x_w)
    AM_RANGE(0xff8a38, 0xff8a39) AM_READWRITE(blitter_count_y_r, blitter_count_y_w)
    AM_RANGE(0xff8a3a, 0xff8a3b) AM_READWRITE(blitter_op_r, blitter_op_w)
    AM_RANGE(0xff8a3c, 0xff8a3d) AM_READWRITE(blitter_ctrl_r, blitter_ctrl_w)*/
	AM_RANGE(0xff9200, 0xff9201) AM_READ_PORT("JOY0")
	AM_RANGE(0xff9202, 0xff9203) AM_READ_PORT("JOY1")
	AM_RANGE(0xff9210, 0xff9211) AM_READ_PORT("PADDLE0X")
	AM_RANGE(0xff9212, 0xff9213) AM_READ_PORT("PADDLE0Y")
	AM_RANGE(0xff9214, 0xff9215) AM_READ_PORT("PADDLE1X")
	AM_RANGE(0xff9216, 0xff9217) AM_READ_PORT("PADDLE1Y")
	AM_RANGE(0xff9220, 0xff9221) AM_READ_PORT("GUNX")
	AM_RANGE(0xff9222, 0xff9223) AM_READ_PORT("GUNY")
ADDRESS_MAP_END


//-------------------------------------------------
//  ADDRESS_MAP( megaste_map )
//-------------------------------------------------

static ADDRESS_MAP_START( megaste_map, AS_PROGRAM, 16, megaste_state )
	AM_IMPORT_FROM(st_map)
/*  AM_RANGE(0xff8204, 0xff8209) AM_READWRITE(shifter_counter_r, shifter_counter_w)
    AM_RANGE(0xff820c, 0xff820d) AM_READWRITE(shifter_base_low_r, shifter_base_low_w)
    AM_RANGE(0xff820e, 0xff820f) AM_READWRITE(shifter_lineofs_r, shifter_lineofs_w)
    AM_RANGE(0xff8264, 0xff8265) AM_READWRITE(shifter_pixelofs_r, shifter_pixelofs_w)
    AM_RANGE(0xff8900, 0xff8901) AM_READWRITE8(sound_dma_control_r, sound_dma_control_w, 0x00ff)
    AM_RANGE(0xff8902, 0xff8907) AM_READWRITE8(sound_dma_base_r, sound_dma_base_w, 0x00ff)
    AM_RANGE(0xff8908, 0xff890d) AM_READ8(sound_dma_counter_r, 0x00ff)
    AM_RANGE(0xff890e, 0xff8913) AM_READWRITE8(sound_dma_end_r, sound_dma_end_w, 0x00ff)
    AM_RANGE(0xff8920, 0xff8921) AM_READWRITE8(sound_mode_r, sound_mode_w, 0x00ff)
    AM_RANGE(0xff8922, 0xff8923) AM_READWRITE(microwire_data_r, microwire_data_w)
    AM_RANGE(0xff8924, 0xff8925) AM_READWRITE(microwire_mask_r, microwire_mask_w)
    AM_RANGE(0xff8a00, 0xff8a1f) AM_READWRITE(blitter_halftone_r, blitter_halftone_w)
    AM_RANGE(0xff8a20, 0xff8a21) AM_READWRITE(blitter_src_inc_x_r, blitter_src_inc_x_w)
    AM_RANGE(0xff8a22, 0xff8a23) AM_READWRITE(blitter_src_inc_y_r, blitter_src_inc_y_w)
    AM_RANGE(0xff8a24, 0xff8a27) AM_READWRITE(blitter_src_r, blitter_src_w)
    AM_RANGE(0xff8a28, 0xff8a2d) AM_READWRITE(blitter_end_mask_r, blitter_end_mask_w)
    AM_RANGE(0xff8a2e, 0xff8a2f) AM_READWRITE(blitter_dst_inc_x_r, blitter_dst_inc_x_w)
    AM_RANGE(0xff8a30, 0xff8a31) AM_READWRITE(blitter_dst_inc_y_r, blitter_dst_inc_y_w)
    AM_RANGE(0xff8a32, 0xff8a35) AM_READWRITE(blitter_dst_r, blitter_dst_w)
    AM_RANGE(0xff8a36, 0xff8a37) AM_READWRITE(blitter_count_x_r, blitter_count_x_w)
    AM_RANGE(0xff8a38, 0xff8a39) AM_READWRITE(blitter_count_y_r, blitter_count_y_w)
    AM_RANGE(0xff8a3a, 0xff8a3b) AM_READWRITE(blitter_op_r, blitter_op_w)
    AM_RANGE(0xff8a3c, 0xff8a3d) AM_READWRITE(blitter_ctrl_r, blitter_ctrl_w)
    AM_RANGE(0xff8e00, 0xff8e0f) AM_READWRITE(vme_r, vme_w)
    AM_RANGE(0xff8e20, 0xff8e21) AM_READWRITE(cache_r, cache_w)
//  AM_RANGE(0xfffa40, 0xfffa5f) AM_READWRITE(fpu_r, fpu_w)*/
	AM_RANGE(0xff8c80, 0xff8c87) AM_DEVREADWRITE8_LEGACY(Z8530_TAG, scc8530_r, scc8530_w, 0x00ff)
	AM_RANGE(0xfffc20, 0xfffc3f) AM_DEVREADWRITE_LEGACY(RP5C15_TAG, rp5c15_r, rp5c15_w)
ADDRESS_MAP_END


//-------------------------------------------------
//  ADDRESS_MAP( stbook_map )
//-------------------------------------------------

static ADDRESS_MAP_START( stbook_map, AS_PROGRAM, 16, stbook_state )
	AM_RANGE(0x000000, 0x1fffff) AM_RAM
	AM_RANGE(0x200000, 0x3fffff) AM_RAM
//  AM_RANGE(0xd40000, 0xd7ffff) AM_ROM
	AM_RANGE(0xe00000, 0xe3ffff) AM_ROM AM_REGION(M68000_TAG, 0)
//  AM_RANGE(0xe80000, 0xebffff) AM_ROM
//  AM_RANGE(0xfa0000, 0xfbffff) AM_ROM // cartridge
	AM_RANGE(0xfc0000, 0xfeffff) AM_ROM AM_REGION(M68000_TAG, 0)
/*  AM_RANGE(0xf00000, 0xf1ffff) AM_READWRITE(stbook_ide_r, stbook_ide_w)
    AM_RANGE(0xff8000, 0xff8001) AM_READWRITE(stbook_mmu_r, stbook_mmu_w)
    AM_RANGE(0xff8200, 0xff8203) AM_READWRITE(stbook_shifter_base_r, stbook_shifter_base_w)
    AM_RANGE(0xff8204, 0xff8209) AM_READWRITE(stbook_shifter_counter_r, stbook_shifter_counter_w)
    AM_RANGE(0xff820a, 0xff820b) AM_READWRITE8(stbook_shifter_sync_r, stbook_shifter_sync_w, 0xff00)
    AM_RANGE(0xff820c, 0xff820d) AM_READWRITE(stbook_shifter_base_low_r, stbook_shifter_base_low_w)
    AM_RANGE(0xff820e, 0xff820f) AM_READWRITE(stbook_shifter_lineofs_r, stbook_shifter_lineofs_w)
    AM_RANGE(0xff8240, 0xff8241) AM_READWRITE(stbook_shifter_palette_r, stbook_shifter_palette_w)
    AM_RANGE(0xff8260, 0xff8261) AM_READWRITE8(stbook_shifter_mode_r, stbook_shifter_mode_w, 0xff00)
    AM_RANGE(0xff8264, 0xff8265) AM_READWRITE(stbook_shifter_pixelofs_r, stbook_shifter_pixelofs_w)
    AM_RANGE(0xff827e, 0xff827f) AM_WRITE(lcd_control_w)*/
	AM_RANGE(0xff8800, 0xff8801) AM_DEVREADWRITE8_LEGACY(YM3439_TAG, ay8910_r, ay8910_data_w, 0xff00)
	AM_RANGE(0xff8802, 0xff8803) AM_DEVWRITE8_LEGACY(YM3439_TAG, ay8910_data_w, 0xff00)
/*  AM_RANGE(0xff8900, 0xff8901) AM_READWRITE8(sound_dma_control_r, sound_dma_control_w, 0x00ff)
    AM_RANGE(0xff8902, 0xff8907) AM_READWRITE8(sound_dma_base_r, sound_dma_base_w, 0x00ff)
    AM_RANGE(0xff8908, 0xff890d) AM_READ8(sound_dma_counter_r, 0x00ff)
    AM_RANGE(0xff890e, 0xff8913) AM_READWRITE8(sound_dma_end_r, sound_dma_end_w, 0x00ff)
    AM_RANGE(0xff8920, 0xff8921) AM_READWRITE8(sound_mode_r, sound_mode_w, 0x00ff)
    AM_RANGE(0xff8922, 0xff8923) AM_READWRITE(microwire_data_r, microwire_data_w)
    AM_RANGE(0xff8924, 0xff8925) AM_READWRITE(microwire_mask_r, microwire_mask_w)
    AM_RANGE(0xff8a00, 0xff8a1f) AM_READWRITE(blitter_halftone_r, blitter_halftone_w)
    AM_RANGE(0xff8a20, 0xff8a21) AM_READWRITE(blitter_src_inc_x_r, blitter_src_inc_x_w)
    AM_RANGE(0xff8a22, 0xff8a23) AM_READWRITE(blitter_src_inc_y_r, blitter_src_inc_y_w)
    AM_RANGE(0xff8a24, 0xff8a27) AM_READWRITE(blitter_src_r, blitter_src_w)
    AM_RANGE(0xff8a28, 0xff8a2d) AM_READWRITE(blitter_end_mask_r, blitter_end_mask_w)
    AM_RANGE(0xff8a2e, 0xff8a2f) AM_READWRITE(blitter_dst_inc_x_r, blitter_dst_inc_x_w)
    AM_RANGE(0xff8a30, 0xff8a31) AM_READWRITE(blitter_dst_inc_y_r, blitter_dst_inc_y_w)
    AM_RANGE(0xff8a32, 0xff8a35) AM_READWRITE(blitter_dst_r, blitter_dst_w)
    AM_RANGE(0xff8a36, 0xff8a37) AM_READWRITE(blitter_count_x_r, blitter_count_x_w)
    AM_RANGE(0xff8a38, 0xff8a39) AM_READWRITE(blitter_count_y_r, blitter_count_y_w)
    AM_RANGE(0xff8a3a, 0xff8a3b) AM_READWRITE(blitter_op_r, blitter_op_w)
    AM_RANGE(0xff8a3c, 0xff8a3d) AM_READWRITE(blitter_ctrl_r, blitter_ctrl_w)
    AM_RANGE(0xff9200, 0xff9201) AM_READ(config_r)
    AM_RANGE(0xff9202, 0xff9203) AM_READWRITE(lcd_contrast_r, lcd_contrast_w)
    AM_RANGE(0xff9210, 0xff9211) AM_READWRITE(power_r, power_w)
    AM_RANGE(0xff9214, 0xff9215) AM_READWRITE(reference_r, reference_w)*/
ADDRESS_MAP_END



//**************************************************************************
//  INPUT PORTS
//**************************************************************************

//-------------------------------------------------
//  INPUT_PORTS( ikbd )
//-------------------------------------------------

static INPUT_PORTS_START( ikbd )
	PORT_START("P31")
	PORT_BIT( 0x10, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_NAME("Control") PORT_CODE(KEYCODE_LCONTROL) PORT_CHAR(UCHAR_MAMEKEY(LCONTROL))
	PORT_BIT( 0xef, IP_ACTIVE_LOW, IPT_UNUSED )

	PORT_START("P32")
	PORT_BIT( 0x01, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_CODE(KEYCODE_F1) PORT_NAME("F1")
	PORT_BIT( 0x20, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_NAME("Left Shift") PORT_CODE(KEYCODE_LSHIFT) PORT_CHAR(UCHAR_SHIFT_1)
	PORT_BIT( 0xde, IP_ACTIVE_LOW, IPT_UNUSED )

	PORT_START("P33")
	PORT_BIT( 0x01, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_CODE(KEYCODE_F2) PORT_NAME("F2")
	PORT_BIT( 0x40, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_NAME(DEF_STR( Alternate )) PORT_CODE(KEYCODE_LALT) PORT_CHAR(UCHAR_MAMEKEY(LALT))
	PORT_BIT( 0xbe, IP_ACTIVE_LOW, IPT_UNUSED )

	PORT_START("P34")
	PORT_BIT( 0x01, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_CODE(KEYCODE_F3) PORT_NAME("F3")
	PORT_BIT( 0x7e, IP_ACTIVE_LOW, IPT_UNUSED )
	PORT_BIT( 0x80, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_NAME("Right Shift") PORT_CODE(KEYCODE_RSHIFT) PORT_CHAR(UCHAR_SHIFT_1)

	PORT_START("P35")
	PORT_BIT( 0x01, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_CODE(KEYCODE_F4) PORT_NAME("F4")
	PORT_BIT( 0x02, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_NAME("Esc") PORT_CODE(KEYCODE_ESC)
	PORT_BIT( 0x04, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_CODE(KEYCODE_1) PORT_CHAR('1') PORT_CHAR('!')
	PORT_BIT( 0x08, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_NAME("Tab") PORT_CODE(KEYCODE_TAB) PORT_CHAR('\t')
	PORT_BIT( 0x10, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_CODE(KEYCODE_Q) PORT_CHAR('Q')
	PORT_BIT( 0x20, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_CODE(KEYCODE_A) PORT_CHAR('A')
	PORT_BIT( 0x40, IP_ACTIVE_LOW, IPT_KEYBOARD )
	PORT_BIT( 0x80, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_CODE(KEYCODE_Z) PORT_CHAR('Z')

	PORT_START("P36")
	PORT_BIT( 0x01, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_CODE(KEYCODE_F5) PORT_NAME("F5")
	PORT_BIT( 0x02, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_CODE(KEYCODE_2) PORT_CHAR('2') PORT_CHAR('@')
	PORT_BIT( 0x04, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_CODE(KEYCODE_3) PORT_CHAR('3') PORT_CHAR('#')
	PORT_BIT( 0x08, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_CODE(KEYCODE_W) PORT_CHAR('W')
	PORT_BIT( 0x10, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_CODE(KEYCODE_E) PORT_CHAR('E')
	PORT_BIT( 0x20, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_CODE(KEYCODE_S) PORT_CHAR('S')
	PORT_BIT( 0x40, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_CODE(KEYCODE_D) PORT_CHAR('D')
	PORT_BIT( 0x80, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_CODE(KEYCODE_X) PORT_CHAR('X')

	PORT_START("P37")
	PORT_BIT( 0x01, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_CODE(KEYCODE_F6) PORT_NAME("F6")
	PORT_BIT( 0x02, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_CODE(KEYCODE_4) PORT_CHAR('4') PORT_CHAR('$')
	PORT_BIT( 0x04, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_CODE(KEYCODE_5) PORT_CHAR('5') PORT_CHAR('%')
	PORT_BIT( 0x08, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_CODE(KEYCODE_R) PORT_CHAR('R')
	PORT_BIT( 0x10, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_CODE(KEYCODE_T) PORT_CHAR('T')
	PORT_BIT( 0x20, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_CODE(KEYCODE_F) PORT_CHAR('F')
	PORT_BIT( 0x40, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_CODE(KEYCODE_C) PORT_CHAR('C')
	PORT_BIT( 0x80, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_CODE(KEYCODE_V) PORT_CHAR('V')

	PORT_START("P40")
	PORT_BIT( 0x01, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_CODE(KEYCODE_F7) PORT_NAME("F7")
	PORT_BIT( 0x02, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_CODE(KEYCODE_6) PORT_CHAR('6') PORT_CHAR('&')
	PORT_BIT( 0x04, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_CODE(KEYCODE_7) PORT_CHAR('7') PORT_CHAR('\'')
	PORT_BIT( 0x08, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_CODE(KEYCODE_Y) PORT_CHAR('Y')
	PORT_BIT( 0x10, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_CODE(KEYCODE_G) PORT_CHAR('G')
	PORT_BIT( 0x20, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_CODE(KEYCODE_H) PORT_CHAR('H')
	PORT_BIT( 0x40, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_CODE(KEYCODE_B) PORT_CHAR('B')
	PORT_BIT( 0x80, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_CODE(KEYCODE_N) PORT_CHAR('N')

	PORT_START("P41")
	PORT_BIT( 0x01, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_CODE(KEYCODE_F8) PORT_NAME("F8")
	PORT_BIT( 0x02, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_CODE(KEYCODE_8) PORT_CHAR('8') PORT_CHAR('(')
	PORT_BIT( 0x04, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_CODE(KEYCODE_9) PORT_CHAR('9') PORT_CHAR(')')
	PORT_BIT( 0x08, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_CODE(KEYCODE_U) PORT_CHAR('U')
	PORT_BIT( 0x10, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_CODE(KEYCODE_I) PORT_CHAR('I')
	PORT_BIT( 0x20, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_CODE(KEYCODE_J) PORT_CHAR('J')
	PORT_BIT( 0x40, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_CODE(KEYCODE_K) PORT_CHAR('K')
	PORT_BIT( 0x80, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_CODE(KEYCODE_M) PORT_CHAR('M')

	PORT_START("P42")
	PORT_BIT( 0x01, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_CODE(KEYCODE_F9) PORT_NAME("F9")
	PORT_BIT( 0x02, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_CODE(KEYCODE_0) PORT_CHAR('0') PORT_CHAR('=')
	PORT_BIT( 0x04, IP_ACTIVE_LOW, IPT_KEYBOARD )
	PORT_BIT( 0x08, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_CODE(KEYCODE_O) PORT_CHAR('O')
	PORT_BIT( 0x10, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_CODE(KEYCODE_P) PORT_CHAR('P')
	PORT_BIT( 0x20, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_CODE(KEYCODE_L) PORT_CHAR('L')
	PORT_BIT( 0x40, IP_ACTIVE_LOW, IPT_KEYBOARD )
	PORT_BIT( 0x80, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_NAME("Space") PORT_CODE(KEYCODE_SPACE) PORT_CHAR(' ')

	PORT_START("P43")
	PORT_BIT( 0x01, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_CODE(KEYCODE_F10) PORT_NAME("F10")
	PORT_BIT( 0x02, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_CODE(KEYCODE_QUOTE) PORT_CHAR(0x00B4) PORT_CHAR('`')
	PORT_BIT( 0x04, IP_ACTIVE_LOW, IPT_KEYBOARD )
	PORT_BIT( 0x08, IP_ACTIVE_LOW, IPT_KEYBOARD )
	PORT_BIT( 0x10, IP_ACTIVE_LOW, IPT_KEYBOARD )
	PORT_BIT( 0x20, IP_ACTIVE_LOW, IPT_KEYBOARD )
	PORT_BIT( 0x40, IP_ACTIVE_LOW, IPT_KEYBOARD )
	PORT_BIT( 0x80, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_NAME("Caps Lock") PORT_CODE(KEYCODE_CAPSLOCK) PORT_CHAR(UCHAR_MAMEKEY(CAPSLOCK))

	PORT_START("P44")
	PORT_BIT( 0x01, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_NAME("Help") PORT_CODE(KEYCODE_F11)
	PORT_BIT( 0x02, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_NAME("Backspace") PORT_CODE(KEYCODE_BACKSPACE) PORT_CHAR(8)
	PORT_BIT( 0x04, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_NAME("Delete") PORT_CODE(KEYCODE_DEL)
	PORT_BIT( 0x08, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_NAME("Insert") PORT_CODE(KEYCODE_INSERT)
	PORT_BIT( 0x10, IP_ACTIVE_LOW, IPT_KEYBOARD )
	PORT_BIT( 0x20, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_NAME("Return") PORT_CODE(KEYCODE_ENTER) PORT_CHAR(13)
	PORT_BIT( 0x40, IP_ACTIVE_LOW, IPT_KEYBOARD )
	PORT_BIT( 0x80, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_CODE(KEYCODE_SLASH) PORT_CHAR('-') PORT_CHAR('_')

	PORT_START("P45")
	PORT_BIT( 0x01, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_NAME("Undo") PORT_CODE(KEYCODE_F12)
	PORT_BIT( 0x02, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_NAME(UTF8_UP) PORT_CODE(KEYCODE_UP) PORT_CHAR(UCHAR_MAMEKEY(UP))
	PORT_BIT( 0x04, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_NAME("Clr Home") PORT_CODE(KEYCODE_HOME) PORT_CHAR(UCHAR_MAMEKEY(HOME))
	PORT_BIT( 0x08, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_NAME(UTF8_LEFT) PORT_CODE(KEYCODE_LEFT) PORT_CHAR(UCHAR_MAMEKEY(LEFT))
	PORT_BIT( 0x10, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_NAME(UTF8_DOWN) PORT_CODE(KEYCODE_DOWN) PORT_CHAR(UCHAR_MAMEKEY(DOWN))
	PORT_BIT( 0x20, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_NAME(UTF8_RIGHT) PORT_CODE(KEYCODE_RIGHT) PORT_CHAR(UCHAR_MAMEKEY(RIGHT))
	PORT_BIT( 0x40, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_NAME("Keypad 1") PORT_CODE(KEYCODE_1_PAD)
	PORT_BIT( 0x80, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_NAME("Keypad 0") PORT_CODE(KEYCODE_0_PAD)

	PORT_START("P46")
	PORT_BIT( 0x01, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_NAME("Keypad (")
	PORT_BIT( 0x02, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_NAME("Keypad )")
	PORT_BIT( 0x04, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_NAME("Keypad 7") PORT_CODE(KEYCODE_7_PAD)
	PORT_BIT( 0x08, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_NAME("Keypad 8") PORT_CODE(KEYCODE_8_PAD)
	PORT_BIT( 0x10, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_NAME("Keypad 4") PORT_CODE(KEYCODE_4_PAD)
	PORT_BIT( 0x20, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_NAME("Keypad 5") PORT_CODE(KEYCODE_5_PAD)
	PORT_BIT( 0x40, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_NAME("Keypad 2") PORT_CODE(KEYCODE_2_PAD)
	PORT_BIT( 0x80, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_NAME("Keypad .") PORT_CODE(KEYCODE_DEL_PAD)

	PORT_START("P47")
	PORT_BIT( 0x01, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_NAME("Keypad /") PORT_CODE(KEYCODE_SLASH_PAD)
	PORT_BIT( 0x02, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_NAME("Keypad *") PORT_CODE(KEYCODE_ASTERISK)
	PORT_BIT( 0x04, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_NAME("Keypad 9") PORT_CODE(KEYCODE_9_PAD)
	PORT_BIT( 0x08, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_NAME("Keypad -") PORT_CODE(KEYCODE_MINUS_PAD)
	PORT_BIT( 0x10, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_NAME("Keypad 6") PORT_CODE(KEYCODE_6_PAD)
	PORT_BIT( 0x20, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_NAME("Keypad +") PORT_CODE(KEYCODE_PLUS_PAD)
	PORT_BIT( 0x40, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_NAME("Keypad 3") PORT_CODE(KEYCODE_3_PAD)
	PORT_BIT( 0x80, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_NAME("Keypad Enter") PORT_CODE(KEYCODE_ENTER_PAD)
INPUT_PORTS_END


//-------------------------------------------------
//  INPUT_PORTS( st )
//-------------------------------------------------

static INPUT_PORTS_START( st )
	PORT_START("config")
	PORT_CATEGORY_CLASS( 0x01, 0x00, "Input Port 0 Device")
	PORT_CATEGORY_ITEM( 0x00, "Mouse", 1 )
	PORT_CATEGORY_ITEM( 0x01, DEF_STR( Joystick ), 2 )
	PORT_CONFNAME( 0x80, 0x80, "Monitor")
	PORT_CONFSETTING( 0x00, "Monochrome (Atari SM124)" )
	PORT_CONFSETTING( 0x80, "Color (Atari SC1224)" )

	PORT_INCLUDE( ikbd )

	PORT_START("IKBD_JOY0")
	PORT_BIT( 0x01, IP_ACTIVE_LOW, IPT_JOYSTICK_LEFT )  PORT_PLAYER(1) PORT_8WAY PORT_CATEGORY(2) // XB
	PORT_BIT( 0x02, IP_ACTIVE_LOW, IPT_JOYSTICK_RIGHT ) PORT_PLAYER(1) PORT_8WAY PORT_CATEGORY(2) // XA
	PORT_BIT( 0x04, IP_ACTIVE_LOW, IPT_JOYSTICK_UP )    PORT_PLAYER(1) PORT_8WAY PORT_CATEGORY(2) // YA
	PORT_BIT( 0x08, IP_ACTIVE_LOW, IPT_JOYSTICK_DOWN )  PORT_PLAYER(1) PORT_8WAY PORT_CATEGORY(2) // YB
	PORT_BIT( 0x10, IP_ACTIVE_LOW, IPT_JOYSTICK_LEFT )  PORT_PLAYER(2) PORT_8WAY PORT_CATEGORY(2)
	PORT_BIT( 0x20, IP_ACTIVE_LOW, IPT_JOYSTICK_RIGHT ) PORT_PLAYER(2) PORT_8WAY PORT_CATEGORY(2)
	PORT_BIT( 0x40, IP_ACTIVE_LOW, IPT_JOYSTICK_UP )    PORT_PLAYER(2) PORT_8WAY PORT_CATEGORY(2)
	PORT_BIT( 0x80, IP_ACTIVE_LOW, IPT_JOYSTICK_DOWN )  PORT_PLAYER(2) PORT_8WAY PORT_CATEGORY(2)

	PORT_START("IKBD_JOY1")
	PORT_BIT( 0x01, IP_ACTIVE_LOW, IPT_BUTTON2 ) PORT_PLAYER(1) PORT_CATEGORY(1)
	PORT_BIT( 0x02, IP_ACTIVE_LOW, IPT_BUTTON1 ) PORT_PLAYER(1)
	PORT_BIT( 0x04, IP_ACTIVE_LOW, IPT_BUTTON1 ) PORT_PLAYER(2) PORT_CATEGORY(2)

	PORT_START("IKBD_MOUSEX")
	PORT_BIT( 0xff, 0x00, IPT_MOUSE_X ) PORT_SENSITIVITY(100) PORT_KEYDELTA(5) PORT_MINMAX(0, 255) PORT_PLAYER(1) PORT_CATEGORY(1)

	PORT_START("IKBD_MOUSEY")
	PORT_BIT( 0xff, 0x00, IPT_MOUSE_Y ) PORT_SENSITIVITY(100) PORT_KEYDELTA(5) PORT_MINMAX(0, 255) PORT_PLAYER(1) PORT_CATEGORY(1)
INPUT_PORTS_END


//-------------------------------------------------
//  INPUT_PORTS( ste )
//-------------------------------------------------

static INPUT_PORTS_START( ste )
	PORT_START("config")
	PORT_CONFNAME( 0x01, 0x00, "Input Port 0 Device")
	PORT_CONFSETTING( 0x00, "Mouse" )
	PORT_CONFSETTING( 0x01, DEF_STR( Joystick ) )
	PORT_CONFNAME( 0x80, 0x80, "Monitor")
	PORT_CONFSETTING( 0x00, "Monochrome (Atari SM124)" )
	PORT_CONFSETTING( 0x80, "Color (Atari SC1435)" )

	PORT_INCLUDE( ikbd )

	PORT_START("JOY0")
	PORT_BIT( 0x01, IP_ACTIVE_LOW, IPT_BUTTON1 ) PORT_PLAYER(1)
	PORT_BIT( 0x02, IP_ACTIVE_LOW, IPT_BUTTON1 ) PORT_PLAYER(3)
	PORT_BIT( 0x04, IP_ACTIVE_LOW, IPT_BUTTON1 ) PORT_PLAYER(2)
	PORT_BIT( 0x08, IP_ACTIVE_LOW, IPT_BUTTON1 ) PORT_PLAYER(4)
	PORT_BIT( 0xf0, IP_ACTIVE_LOW, IPT_UNUSED )

	PORT_START("JOY1")
	PORT_BIT( 0x0001, IP_ACTIVE_LOW, IPT_JOYSTICK_RIGHT ) PORT_PLAYER(1) PORT_8WAY
	PORT_BIT( 0x0002, IP_ACTIVE_LOW, IPT_JOYSTICK_LEFT )  PORT_PLAYER(1) PORT_8WAY
	PORT_BIT( 0x0004, IP_ACTIVE_LOW, IPT_JOYSTICK_DOWN )  PORT_PLAYER(1) PORT_8WAY
	PORT_BIT( 0x0008, IP_ACTIVE_LOW, IPT_JOYSTICK_UP )    PORT_PLAYER(1) PORT_8WAY
	PORT_BIT( 0x0010, IP_ACTIVE_LOW, IPT_JOYSTICK_RIGHT ) PORT_PLAYER(2) PORT_8WAY
	PORT_BIT( 0x0020, IP_ACTIVE_LOW, IPT_JOYSTICK_LEFT )  PORT_PLAYER(2) PORT_8WAY
	PORT_BIT( 0x0040, IP_ACTIVE_LOW, IPT_JOYSTICK_DOWN )  PORT_PLAYER(2) PORT_8WAY
	PORT_BIT( 0x0080, IP_ACTIVE_LOW, IPT_JOYSTICK_UP )    PORT_PLAYER(2) PORT_8WAY
	PORT_BIT( 0x0100, IP_ACTIVE_LOW, IPT_JOYSTICK_RIGHT ) PORT_PLAYER(3) PORT_8WAY
	PORT_BIT( 0x0200, IP_ACTIVE_LOW, IPT_JOYSTICK_LEFT )  PORT_PLAYER(3) PORT_8WAY
	PORT_BIT( 0x0400, IP_ACTIVE_LOW, IPT_JOYSTICK_DOWN )  PORT_PLAYER(3) PORT_8WAY
	PORT_BIT( 0x0800, IP_ACTIVE_LOW, IPT_JOYSTICK_UP )    PORT_PLAYER(3) PORT_8WAY
	PORT_BIT( 0x1000, IP_ACTIVE_LOW, IPT_JOYSTICK_RIGHT ) PORT_PLAYER(4) PORT_8WAY
	PORT_BIT( 0x2000, IP_ACTIVE_LOW, IPT_JOYSTICK_LEFT )  PORT_PLAYER(4) PORT_8WAY
	PORT_BIT( 0x4000, IP_ACTIVE_LOW, IPT_JOYSTICK_DOWN )  PORT_PLAYER(4) PORT_8WAY
	PORT_BIT( 0x8000, IP_ACTIVE_LOW, IPT_JOYSTICK_UP )    PORT_PLAYER(4) PORT_8WAY

	PORT_START("PADDLE0X")
	PORT_BIT( 0xff, 0x00, IPT_PADDLE ) PORT_SENSITIVITY(30) PORT_KEYDELTA(15) PORT_PLAYER(1)

	PORT_START("PADDLE0Y")
	PORT_BIT( 0xff, 0x00, IPT_PADDLE_V ) PORT_SENSITIVITY(30) PORT_KEYDELTA(15) PORT_PLAYER(1)

	PORT_START("PADDLE1X")
	PORT_BIT( 0xff, 0x00, IPT_PADDLE ) PORT_SENSITIVITY(30) PORT_KEYDELTA(15) PORT_PLAYER(2)

	PORT_START("PADDLE1Y")
	PORT_BIT( 0xff, 0x00, IPT_PADDLE_V ) PORT_SENSITIVITY(30) PORT_KEYDELTA(15) PORT_PLAYER(2)

	PORT_START("GUNX") // should be 10-bit
	PORT_BIT( 0xff, 0x80, IPT_LIGHTGUN_X ) PORT_CROSSHAIR(X, 1.0, 0.0, 0) PORT_SENSITIVITY(50) PORT_KEYDELTA(10) PORT_PLAYER(1)

	PORT_START("GUNY") // should be 10-bit
	PORT_BIT( 0xff, 0x80, IPT_LIGHTGUN_Y ) PORT_CROSSHAIR(Y, 1.0, 0.0, 0) PORT_SENSITIVITY(70) PORT_KEYDELTA(10) PORT_PLAYER(1)
INPUT_PORTS_END


//-------------------------------------------------
//  INPUT_PORTS( stbook )
//-------------------------------------------------

static INPUT_PORTS_START( stbook )
	PORT_START("SW400")
	PORT_DIPNAME( 0x80, 0x80, "DMA sound hardware")
	PORT_DIPSETTING( 0x00, DEF_STR( No ) )
	PORT_DIPSETTING( 0x80, DEF_STR( Yes ) )
	PORT_DIPNAME( 0x40, 0x00, "WD1772 FDC")
	PORT_DIPSETTING( 0x40, "Low Speed (8 MHz)" )
	PORT_DIPSETTING( 0x00, "High Speed (16 MHz)" )
	PORT_DIPNAME( 0x20, 0x00, "Bypass Self Test")
	PORT_DIPSETTING( 0x00, DEF_STR( No ) )
	PORT_DIPSETTING( 0x20, DEF_STR( Yes ) )
	PORT_BIT( 0x1f, IP_ACTIVE_HIGH, IPT_UNUSED )
INPUT_PORTS_END


//-------------------------------------------------
//  INPUT_PORTS( tt030 )
//-------------------------------------------------

static INPUT_PORTS_START( tt030 )
	PORT_INCLUDE(ste)
INPUT_PORTS_END


//-------------------------------------------------
//  INPUT_PORTS( falcon )
//-------------------------------------------------

static INPUT_PORTS_START( falcon )
	PORT_INCLUDE(ste)
INPUT_PORTS_END



//**************************************************************************
//  DEVICE CONFIGURATION
//**************************************************************************

//-------------------------------------------------
//  ay8910_interface psg_intf
//-------------------------------------------------

WRITE8_MEMBER( st_state::psg_pa_w )
{
	/*

        bit     description

        0       SIDE 0
        1       DRIVE 0
        2       DRIVE 1
        3       RTS
        4       DTR
        5       STROBE
        6       GPO
        7

    */

	// side select
	wd17xx_set_side(m_fdc, BIT(data, 0) ? 0 : 1);

	// drive select
	if (!BIT(data, 1)) wd17xx_set_drive(m_fdc, 0);
	if (!BIT(data, 2)) wd17xx_set_drive(m_fdc, 1);

	// request to send
	rs232_rts_w(m_rs232, BIT(data, 3));

	// data terminal ready
	rs232_dtr_w(m_rs232, BIT(data, 4));

	// centronics strobe
	centronics_strobe_w(m_centronics, BIT(data, 5));
}

static const ay8910_interface psg_intf =
{
	AY8910_SINGLE_OUTPUT,
	{ RES_K(1) },
	DEVCB_NULL,
	DEVCB_NULL,
	DEVCB_DRIVER_MEMBER(st_state, psg_pa_w),
	DEVCB_DEVICE_HANDLER(CENTRONICS_TAG, centronics_data_w)
};


//-------------------------------------------------
//  ay8910_interface stbook_psg_intf
//-------------------------------------------------

WRITE8_MEMBER( stbook_state::psg_pa_w )
{
	/*

        bit     description

        0       SIDE 0
        1       DRIVE 0
        2       DRIVE 1
        3       RTS
        4       DTR
        5       STROBE
        6       IDE RESET
        7       DDEN

    */

	// side select
	wd17xx_set_side(m_fdc, BIT(data, 0) ? 0 : 1);

	// drive select
	if (!BIT(data, 1)) wd17xx_set_drive(m_fdc, 0);
	if (!BIT(data, 2)) wd17xx_set_drive(m_fdc, 1);

	// request to send
	rs232_rts_w(m_rs232, BIT(data, 3));

	// data terminal ready
	rs232_dtr_w(m_rs232, BIT(data, 4));

	// centronics strobe
	centronics_strobe_w(m_centronics, BIT(data, 5));

	// density select
	wd17xx_dden_w(m_fdc, BIT(data, 7));
}

static const ay8910_interface stbook_psg_intf =
{
	AY8910_SINGLE_OUTPUT,
	{ RES_K(1) },
	DEVCB_NULL,
	DEVCB_NULL,
	DEVCB_DRIVER_MEMBER(stbook_state, psg_pa_w),
	DEVCB_DEVICE_HANDLER(CENTRONICS_TAG, centronics_data_w)
};


//-------------------------------------------------
//  ACIA6850_INTERFACE( acia_ikbd_intf )
//-------------------------------------------------

READ_LINE_MEMBER( st_state::ikbd_rx_r )
{
	return m_ikbd_rx;
}

WRITE_LINE_MEMBER( st_state::ikbd_tx_w )
{
	m_ikbd_tx = state;
}

WRITE_LINE_MEMBER( st_state::acia_ikbd_irq_w )
{
	m_acia_ikbd_irq = state;

	m_mfp->i4_w(m_acia_ikbd_irq & m_acia_midi_irq);
}

static ACIA6850_INTERFACE( acia_ikbd_intf )
{
	Y2/64,
	Y2/64,
	DEVCB_DRIVER_LINE_MEMBER(st_state, ikbd_rx_r),
	DEVCB_DRIVER_LINE_MEMBER(st_state, ikbd_tx_w),
	DEVCB_NULL,
	DEVCB_NULL,
	DEVCB_NULL,
	DEVCB_DRIVER_LINE_MEMBER(st_state, acia_ikbd_irq_w)
};


//-------------------------------------------------
//  ACIA6850_INTERFACE( stbook_acia_ikbd_intf )
//-------------------------------------------------

static ACIA6850_INTERFACE( stbook_acia_ikbd_intf )
{
	U517/2/16, // 500kHz
	U517/2/2, // 1MHZ
	DEVCB_DRIVER_LINE_MEMBER(st_state, ikbd_rx_r),
	DEVCB_DRIVER_LINE_MEMBER(st_state, ikbd_tx_w),
	DEVCB_NULL,
	DEVCB_NULL,
	DEVCB_NULL,
	DEVCB_DRIVER_LINE_MEMBER(st_state, acia_ikbd_irq_w)
};


//-------------------------------------------------
//  ACIA6850_INTERFACE( acia_midi_intf )
//-------------------------------------------------

WRITE_LINE_MEMBER( st_state::acia_midi_irq_w )
{
	m_acia_midi_irq = state;

	m_mfp->i4_w(m_acia_ikbd_irq & m_acia_midi_irq);
}

static ACIA6850_INTERFACE( acia_midi_intf )
{
	Y2/64,
	Y2/64,
	DEVCB_NULL,
	DEVCB_NULL,
	DEVCB_NULL,
	DEVCB_NULL,
	DEVCB_NULL,
	DEVCB_DRIVER_LINE_MEMBER(st_state, acia_midi_irq_w)
};


//-------------------------------------------------
//  MC68901_INTERFACE( mfp_intf )
//-------------------------------------------------

READ8_MEMBER( st_state::mfp_gpio_r )
{
	/*

        bit     description

        0       Centronics BUSY
        1       RS232 DCD
        2       RS232 CTS
        3       Blitter done
        4       Keyboard/MIDI
        5       FDC
        6       RS232 RI
        7       Monochrome monitor detect

    */

	UINT8 data = 0;

	// centronics busy
	data |= centronics_busy_r(m_centronics);

	// data carrier detect
	data |= rs232_dcd_r(m_rs232) << 1;

	// clear to send
	data |= rs232_cts_r(m_rs232) << 2;

	// blitter done
	data |= m_blitter_done << 3;

	// keyboard/MIDI interrupt
	data |= (m_acia_ikbd_irq & m_acia_midi_irq) << 4;

	// floppy data request
	data |= !wd17xx_intrq_r(m_fdc) << 5;

	// ring indicator
	data |= rs232_ri_r(m_rs232) << 6;

	// monochrome monitor detect
	data |= input_port_read(machine(), "config") & 0x80;

	return data;
}

WRITE_LINE_MEMBER( st_state::mfp_tdo_w )
{
	m_mfp->rc_w(state);
	m_mfp->tc_w(state);
}

WRITE_LINE_MEMBER( st_state::mfp_so_w )
{
	rs232_td_w(m_rs232, m_mfp, state);
}

static MC68901_INTERFACE( mfp_intf )
{
	Y1,													/* timer clock */
	0,													/* receive clock */
	0,													/* transmit clock */
	DEVCB_CPU_INPUT_LINE(M68000_TAG, M68K_IRQ_6),		/* interrupt */
	DEVCB_DRIVER_MEMBER(st_state, mfp_gpio_r),			/* GPIO read */
	DEVCB_NULL,											/* GPIO write */
	DEVCB_NULL,											/* TAO */
	DEVCB_NULL,											/* TBO */
	DEVCB_NULL,											/* TCO */
	DEVCB_DRIVER_LINE_MEMBER(st_state, mfp_tdo_w),		/* TDO */
	DEVCB_DEVICE_LINE(RS232_TAG, rs232_rd_r),			/* serial input */
	DEVCB_DRIVER_LINE_MEMBER(st_state, mfp_so_w)		/* serial output */
};


//-------------------------------------------------
//  MC68901_INTERFACE( atariste_mfp_intf )
//-------------------------------------------------

READ8_MEMBER( ste_state::mfp_gpio_r )
{
	/*

        bit     description

        0       Centronics BUSY
        1       RS232 DCD
        2       RS232 CTS
        3       Blitter done
        4       Keyboard/MIDI
        5       FDC
        6       RS232 RI
        7       Monochrome monitor detect / DMA sound active

    */

	UINT8 data = 0;

	// centronics busy
	data |= centronics_busy_r(m_centronics);

	// data carrier detect
	data |= rs232_dcd_r(m_rs232) << 1;

	// clear to send
	data |= rs232_cts_r(m_rs232) << 2;

	// blitter done
	data |= m_blitter_done << 3;

	// keyboard/MIDI interrupt
	data |= (m_acia_ikbd_irq & m_acia_midi_irq) << 4;

	// floppy data request
	data |= !wd17xx_intrq_r(m_fdc) << 5;

	// ring indicator
	data |= rs232_ri_r(m_rs232) << 6;

	// monochrome monitor detect, DMA sound active
	data |= (input_port_read(machine(), "config") & 0x80) ^ (m_dmasnd_active << 7);

	return data;
}

static MC68901_INTERFACE( atariste_mfp_intf )
{
	Y1,													/* timer clock */
	0,													/* receive clock */
	0,													/* transmit clock */
	DEVCB_CPU_INPUT_LINE(M68000_TAG, M68K_IRQ_6),		/* interrupt */
	DEVCB_DRIVER_MEMBER(ste_state, mfp_gpio_r),			/* GPIO read */
	DEVCB_NULL,											/* GPIO write */
	DEVCB_NULL,											/* TAO */
	DEVCB_NULL,											/* TBO */
	DEVCB_NULL,											/* TCO */
	DEVCB_DRIVER_LINE_MEMBER(st_state, mfp_tdo_w),		/* TDO */
	DEVCB_DEVICE_LINE(RS232_TAG, rs232_rd_r),			/* serial input */
	DEVCB_DRIVER_LINE_MEMBER(st_state, mfp_so_w)		/* serial output */
};


//-------------------------------------------------
//  MC68901_INTERFACE( stbook_mfp_intf )
//-------------------------------------------------

READ8_MEMBER( stbook_state::mfp_gpio_r )
{
	/*

        bit     description

        0       Centronics BUSY
        1       RS232 DCD
        2       RS232 CTS
        3       Blitter done
        4       Keyboard/MIDI
        5       FDC
        6       RS232 RI
        7       POWER ALARMS

    */

	UINT8 data = 0;

	// centronics busy
	data |= centronics_busy_r(m_centronics);

	// data carrier detect
	data |= rs232_dcd_r(m_rs232) << 1;

	// clear to send
	data |= rs232_cts_r(m_rs232) << 2;

	// blitter done
	data |= m_blitter_done << 3;

	// keyboard/MIDI interrupt
	data |= (m_acia_ikbd_irq & m_acia_midi_irq) << 4;

	// floppy data request
	data |= !wd17xx_intrq_r(m_fdc) << 5;

	// ring indicator
	data |= rs232_ri_r(m_rs232) << 6;

	// TODO power alarms

	return data;
}

static MC68901_INTERFACE( stbook_mfp_intf )
{
	Y1,													/* timer clock */
	0,													/* receive clock */
	0,													/* transmit clock */
	DEVCB_CPU_INPUT_LINE(M68000_TAG, M68K_IRQ_6),		/* interrupt */
	DEVCB_DRIVER_MEMBER(stbook_state, mfp_gpio_r),		/* GPIO read */
	DEVCB_NULL,											/* GPIO write */
	DEVCB_NULL,											/* TAO */
	DEVCB_NULL,											/* TBO */
	DEVCB_NULL,											/* TCO */
	DEVCB_DRIVER_LINE_MEMBER(st_state, mfp_tdo_w),		/* TDO */
	DEVCB_DEVICE_LINE(RS232_TAG, rs232_rd_r),			/* serial input */
	DEVCB_DRIVER_LINE_MEMBER(st_state, mfp_so_w)		/* serial output */
};



//-------------------------------------------------
//  wd17xx_interface fdc_intf
//-------------------------------------------------

static const floppy_config atarist_floppy_config =
{
	DEVCB_NULL,
	DEVCB_NULL,
	DEVCB_NULL,
	DEVCB_NULL,
	DEVCB_NULL,
	FLOPPY_STANDARD_3_5_DSDD,
	FLOPPY_OPTIONS_NAME(atarist),
	NULL
};

static const floppy_config megaste_floppy_config =
{
	DEVCB_NULL,
	DEVCB_NULL,
	DEVCB_NULL,
	DEVCB_NULL,
	DEVCB_NULL,
	FLOPPY_STANDARD_3_5_DSHD,
	FLOPPY_OPTIONS_NAME(atarist),
	NULL
};

WRITE_LINE_MEMBER( st_state::fdc_intrq_w )
{
	m_mfp->i5_w(!state);
}

WRITE_LINE_MEMBER( st_state::fdc_drq_w )
{
	if (state && (!(m_fdc_mode & DMA_MODE_ENABLED)) && (m_fdc_mode & DMA_MODE_FDC_HDC_ACK))
	{
		fdc_dma_transfer();
	}
}

static const wd17xx_interface fdc_intf =
{
	DEVCB_LINE_GND,
	DEVCB_DRIVER_LINE_MEMBER(st_state, fdc_intrq_w),
	DEVCB_DRIVER_LINE_MEMBER(st_state, fdc_drq_w),
	{ FLOPPY_0, FLOPPY_1, NULL, NULL }
};


//-------------------------------------------------
//  wd17xx_interface stbook_fdc_intf
//-------------------------------------------------

static const wd17xx_interface stbook_fdc_intf =
{
	DEVCB_NULL,
	DEVCB_DRIVER_LINE_MEMBER(st_state, fdc_intrq_w),
	DEVCB_DRIVER_LINE_MEMBER(st_state, fdc_drq_w),
	{ FLOPPY_0, FLOPPY_1, NULL, NULL }
};


//-------------------------------------------------
//  RS232_INTERFACE( rs232_intf )
//-------------------------------------------------

static RS232_INTERFACE( rs232_intf )
{
	{ MC68901_TAG },
	{ NULL }
};


//-------------------------------------------------
//  rp5c15_interface rtc_intf
//-------------------------------------------------

static const struct rp5c15_interface rtc_intf =
{
	NULL
};


//-------------------------------------------------
//  centronics_interface centronics_intf
//-------------------------------------------------

static const centronics_interface centronics_intf =
{
	0,
	DEVCB_NULL,
	DEVCB_DEVICE_LINE_MEMBER(MC68901_TAG, mc68901_device, i0_w),
	DEVCB_NULL
};



//**************************************************************************
//  MACHINE INITIALIZATION
//**************************************************************************

//-------------------------------------------------
//  IRQ_CALLBACK( atarist_int_ack )
//-------------------------------------------------

static IRQ_CALLBACK( atarist_int_ack )
{
	st_state *state = device->machine().driver_data<st_state>();

	if (irqline == M68K_IRQ_6)
	{
		return state->m_mfp->get_vector();
	}

	return M68K_INT_ACK_AUTOVECTOR;
}


//-------------------------------------------------
//  configure_memory -
//-------------------------------------------------

void st_state::configure_memory()
{
	address_space *program = m_maincpu->memory().space(AS_PROGRAM);

	switch (ram_get_size(m_ram))
	{
	case 256 * 1024:
		program->unmap_readwrite(0x040000, 0x3fffff);
		break;

	case 512 * 1024:
		program->unmap_readwrite(0x080000, 0x3fffff);
		break;

	case 1024 * 1024:
		program->unmap_readwrite(0x100000, 0x3fffff);
		break;

	case 2048 * 1024:
		program->unmap_readwrite(0x200000, 0x3fffff);
		break;
	}
}

//-------------------------------------------------
//  state_save -
//-------------------------------------------------

void st_state::state_save()
{
	m_dma_error = 1;

	state_save_register_global(machine(), m_mmu);
	state_save_register_global(machine(), m_dma_base);
	state_save_register_global(machine(), m_dma_error);
	state_save_register_global(machine(), m_fdc_mode);
	state_save_register_global(machine(), m_fdc_sectors);
	state_save_register_global(machine(), m_fdc_dmabytes);
	state_save_register_global(machine(), m_ikbd_keylatch);
	state_save_register_global(machine(), m_ikbd_mouse);
	state_save_register_global(machine(), m_ikbd_mouse_x);
	state_save_register_global(machine(), m_ikbd_mouse_y);
	state_save_register_global(machine(), m_ikbd_mouse_px);
	state_save_register_global(machine(), m_ikbd_mouse_py);
	state_save_register_global(machine(), m_ikbd_mouse_pc);
	state_save_register_global(machine(), m_ikbd_rx);
	state_save_register_global(machine(), m_ikbd_tx);
	state_save_register_global(machine(), m_ikbd_joy);
	state_save_register_global(machine(), m_midi_rx);
	state_save_register_global(machine(), m_midi_tx);
	state_save_register_global(machine(), m_acia_ikbd_irq);
	state_save_register_global(machine(), m_acia_midi_irq);
}


//-------------------------------------------------
//  MACHINE_START( st )
//-------------------------------------------------

void st_state::machine_start()
{
	// configure RAM banking
	configure_memory();

	// set CPU interrupt callback
	device_set_irq_callback(m_maincpu, atarist_int_ack);

	// allocate timers
	m_mouse_timer = machine().scheduler().timer_alloc(FUNC(st_mouse_tick));
	m_mouse_timer->adjust(attotime::zero, 0, attotime::from_hz(500));

	// register for state saving
	state_save();
}


//-------------------------------------------------
//  state_save -
//-------------------------------------------------

void ste_state::state_save()
{
	st_state::state_save();

	state_save_register_global(machine(), m_dmasnd_base);
	state_save_register_global(machine(), m_dmasnd_end);
	state_save_register_global(machine(), m_dmasnd_cntr);
	state_save_register_global(machine(), m_dmasnd_baselatch);
	state_save_register_global(machine(), m_dmasnd_endlatch);
	state_save_register_global(machine(), m_dmasnd_ctrl);
	state_save_register_global(machine(), m_dmasnd_mode);
	state_save_register_global_array(machine(), m_dmasnd_fifo);
	state_save_register_global(machine(), m_dmasnd_samples);
	state_save_register_global(machine(), m_dmasnd_active);
	state_save_register_global(machine(), m_mw_data);
	state_save_register_global(machine(), m_mw_mask);
	state_save_register_global(machine(), m_mw_shift);
}


//-------------------------------------------------
//  MACHINE_START( ste )
//-------------------------------------------------

void ste_state::machine_start()
{
	/* configure RAM banking */
	configure_memory();

	/* set CPU interrupt callback */
	device_set_irq_callback(m_maincpu, atarist_int_ack);

	/* allocate timers */
	m_dmasound_timer = machine().scheduler().timer_alloc(FUNC(atariste_dmasound_tick));
	m_microwire_timer = machine().scheduler().timer_alloc(FUNC(atariste_microwire_tick));

	/* register for state saving */
	state_save();
}


//-------------------------------------------------
//  MACHINE_START( megaste )
//-------------------------------------------------

void megaste_state::machine_start()
{
	ste_state::machine_start();

	state_save_register_global(machine(), m_cache);
}


//-------------------------------------------------
//  MACHINE_START( stbook )
//-------------------------------------------------

void stbook_state::machine_start()
{
	/* configure RAM banking */
	address_space *program = m_maincpu->memory().space(AS_PROGRAM);

	switch (ram_get_size(m_ram))
	{
	case 1024 * 1024:
		program->unmap_readwrite(0x100000, 0x3fffff);
		break;
	}

	/* set CPU interrupt callback */
	device_set_irq_callback(m_maincpu, atarist_int_ack);

	/* register for state saving */
	ste_state::state_save();
}



//**************************************************************************
//  MACHINE CONFIGURATION
//**************************************************************************

//-------------------------------------------------
//  MACHINE_CONFIG( st )
//-------------------------------------------------

static MACHINE_CONFIG_START( st, st_state )
	// basic machine hardware
	MCFG_CPU_ADD(M68000_TAG, M68000, Y2/4)
	MCFG_CPU_PROGRAM_MAP(st_map)

	MCFG_CPU_ADD(HD6301V1_TAG, HD6301, XTAL_4MHz)
	MCFG_CPU_PROGRAM_MAP(ikbd_map)
	MCFG_CPU_IO_MAP(ikbd_io_map)

	// video hardware
	MCFG_SCREEN_ADD(SCREEN_TAG, RASTER)
	MCFG_SCREEN_FORMAT(BITMAP_FORMAT_RGB32)
	MCFG_SCREEN_RAW_PARAMS(Y2/4, ATARIST_HTOT_PAL, ATARIST_HBEND_PAL, ATARIST_HBSTART_PAL, ATARIST_VTOT_PAL, ATARIST_VBEND_PAL, ATARIST_VBSTART_PAL)
	MCFG_SCREEN_UPDATE(generic_bitmapped)

	MCFG_PALETTE_LENGTH(16)

	// sound hardware
	MCFG_SPEAKER_STANDARD_MONO("mono")
	MCFG_SOUND_ADD(YM2149_TAG, YM2149, Y2/16)
	MCFG_SOUND_CONFIG(psg_intf)
	MCFG_SOUND_ROUTE(ALL_OUTPUTS, "mono", 1.00)

	// devices
	MCFG_ACIA6850_ADD(MC6850_0_TAG, acia_ikbd_intf)
	MCFG_ACIA6850_ADD(MC6850_1_TAG, acia_midi_intf)
	MCFG_MC68901_ADD(MC68901_TAG, Y2/8, mfp_intf)
	MCFG_WD1772_ADD(WD1772_TAG, fdc_intf)
	MCFG_FLOPPY_2_DRIVES_ADD(atarist_floppy_config)
	MCFG_RS232_ADD(RS232_TAG, rs232_intf)
	MCFG_CENTRONICS_ADD(CENTRONICS_TAG, centronics_intf)

	// cartridge
	MCFG_CARTSLOT_ADD("cart")
	MCFG_CARTSLOT_EXTENSION_LIST("bin,rom")
	MCFG_CARTSLOT_NOT_MANDATORY
	MCFG_CARTSLOT_INTERFACE("st_cart")
	MCFG_SOFTWARE_LIST_ADD("cart_list", "st")

	// internal ram
	MCFG_RAM_ADD(RAM_TAG)
	MCFG_RAM_DEFAULT_SIZE("1M")  // 1040ST
	MCFG_RAM_EXTRA_OPTIONS("512K,256K") // 520ST, 260ST
MACHINE_CONFIG_END


//-------------------------------------------------
//  MACHINE_CONFIG( megast )
//-------------------------------------------------

static MACHINE_CONFIG_START( megast, megast_state )
	// basic machine hardware
	MCFG_CPU_ADD(M68000_TAG, M68000, Y2/4)
	MCFG_CPU_PROGRAM_MAP(megast_map)

	MCFG_CPU_ADD(HD6301V1_TAG, HD6301, XTAL_4MHz)
	MCFG_CPU_PROGRAM_MAP(ikbd_map)
	MCFG_CPU_IO_MAP(ikbd_io_map)

	// video hardware
	MCFG_SCREEN_ADD(SCREEN_TAG, RASTER)
	MCFG_SCREEN_FORMAT(BITMAP_FORMAT_RGB32)
	MCFG_SCREEN_RAW_PARAMS(Y2/4, ATARIST_HTOT_PAL, ATARIST_HBEND_PAL, ATARIST_HBSTART_PAL, ATARIST_VTOT_PAL, ATARIST_VBEND_PAL, ATARIST_VBSTART_PAL)
	MCFG_SCREEN_UPDATE(generic_bitmapped)

	MCFG_PALETTE_LENGTH(16)

	// sound hardware
	MCFG_SPEAKER_STANDARD_MONO("mono")
	MCFG_SOUND_ADD(YM2149_TAG, YM2149, Y2/16)
	MCFG_SOUND_CONFIG(psg_intf)
	MCFG_SOUND_ROUTE(ALL_OUTPUTS, "mono", 1.00)

	// devices
	MCFG_ACIA6850_ADD(MC6850_0_TAG, acia_ikbd_intf)
	MCFG_ACIA6850_ADD(MC6850_1_TAG, acia_midi_intf)
	MCFG_MC68901_ADD(MC68901_TAG, Y2/8, mfp_intf)
	MCFG_WD1772_ADD(WD1772_TAG, fdc_intf)
	MCFG_FLOPPY_2_DRIVES_ADD(atarist_floppy_config)
	MCFG_RS232_ADD(RS232_TAG, rs232_intf)
	MCFG_CENTRONICS_ADD(CENTRONICS_TAG, centronics_intf)
	MCFG_RP5C15_ADD(RP5C15_TAG, rtc_intf)

	// cartridge
	MCFG_CARTSLOT_ADD("cart")
	MCFG_CARTSLOT_EXTENSION_LIST("bin,rom")
	MCFG_CARTSLOT_NOT_MANDATORY
	MCFG_CARTSLOT_INTERFACE("st_cart")
	MCFG_SOFTWARE_LIST_ADD("cart_list", "st")

	// internal ram
	MCFG_RAM_ADD(RAM_TAG)
	MCFG_RAM_DEFAULT_SIZE("4M")  //  Mega ST 4
	MCFG_RAM_EXTRA_OPTIONS("2M,1M") //  Mega ST 2 ,Mega ST 1
MACHINE_CONFIG_END


//-------------------------------------------------
//  MACHINE_CONFIG( ste )
//-------------------------------------------------

static MACHINE_CONFIG_START( ste, ste_state )
	// basic machine hardware
	MCFG_CPU_ADD(M68000_TAG, M68000, Y2/4)
	MCFG_CPU_PROGRAM_MAP(ste_map)

	MCFG_CPU_ADD(HD6301V1_TAG, HD6301, XTAL_4MHz)
	MCFG_CPU_PROGRAM_MAP(ikbd_map)
	MCFG_CPU_IO_MAP(ikbd_io_map)

	// video hardware
	MCFG_SCREEN_ADD(SCREEN_TAG, RASTER)
	MCFG_SCREEN_FORMAT(BITMAP_FORMAT_RGB32)
	MCFG_SCREEN_UPDATE( generic_bitmapped )
	MCFG_SCREEN_RAW_PARAMS(Y2/4, ATARIST_HTOT_PAL, ATARIST_HBEND_PAL, ATARIST_HBSTART_PAL, ATARIST_VTOT_PAL, ATARIST_VBEND_PAL, ATARIST_VBSTART_PAL)

	MCFG_PALETTE_LENGTH(512)
	
	// sound hardware
	MCFG_SPEAKER_STANDARD_STEREO("lspeaker", "rspeaker")

	MCFG_SOUND_ADD(YM2149_TAG, YM2149, Y2/16)
	MCFG_SOUND_CONFIG(psg_intf)
	MCFG_SOUND_ROUTE(0, "lspeaker", 0.50)
	MCFG_SOUND_ROUTE(0, "rspeaker", 0.50)
/*
    MCFG_SOUND_ADD("custom", CUSTOM, 0) // DAC
    MCFG_SOUND_ROUTE(0, "rspeaker", 0.50)
    MCFG_SOUND_ROUTE(1, "lspeaker", 0.50)
*/
	MCFG_LMC1992_ADD(LMC1992_TAG /* ,atariste_lmc1992_intf */)

	// devices
	MCFG_ACIA6850_ADD(MC6850_0_TAG, acia_ikbd_intf)
	MCFG_ACIA6850_ADD(MC6850_1_TAG, acia_midi_intf)
	MCFG_MC68901_ADD(MC68901_TAG, Y2/8, atariste_mfp_intf)
	MCFG_WD1772_ADD(WD1772_TAG, fdc_intf )
	MCFG_FLOPPY_2_DRIVES_ADD(atarist_floppy_config)
	MCFG_CENTRONICS_ADD(CENTRONICS_TAG, centronics_intf)
	MCFG_RS232_ADD(RS232_TAG, rs232_intf)

	// cartridge
	MCFG_CARTSLOT_ADD("cart")
	MCFG_CARTSLOT_EXTENSION_LIST("bin,rom")
	MCFG_CARTSLOT_NOT_MANDATORY
	MCFG_CARTSLOT_INTERFACE("st_cart")
//  MCFG_SOFTWARE_LIST_ADD("cart_list", "ste")

	// internal ram
	MCFG_RAM_ADD(RAM_TAG)
	MCFG_RAM_DEFAULT_SIZE("1M")  // 1040STe
	MCFG_RAM_EXTRA_OPTIONS("512K") //  520STe
MACHINE_CONFIG_END


//-------------------------------------------------
//  MACHINE_CONFIG( megaste )
//-------------------------------------------------

static MACHINE_CONFIG_DERIVED( megaste, ste )
	MCFG_CPU_MODIFY(M68000_TAG)
	MCFG_CPU_PROGRAM_MAP(megaste_map)
	MCFG_RP5C15_ADD(RP5C15_TAG, rtc_intf)
	MCFG_SCC8530_ADD(Z8530_TAG, Y2/4)

	MCFG_FLOPPY_2_DRIVES_MODIFY(megaste_floppy_config)

	/* internal ram */
	MCFG_RAM_MODIFY(RAM_TAG)
	MCFG_RAM_DEFAULT_SIZE("4M")  //  Mega STe 4
	MCFG_RAM_EXTRA_OPTIONS("2M,1M") //  Mega STe 2 ,Mega STe 1
MACHINE_CONFIG_END


//-------------------------------------------------
//  MACHINE_CONFIG( stbook )
//-------------------------------------------------

static MACHINE_CONFIG_START( stbook, stbook_state )
	// basic machine hardware
	MCFG_CPU_ADD(M68000_TAG, M68000, U517/2)
	MCFG_CPU_PROGRAM_MAP(stbook_map)

	//MCFG_CPU_ADD(COP888_TAG, COP888, Y700)

	// video hardware
	MCFG_SCREEN_ADD(SCREEN_TAG, LCD)
	MCFG_SCREEN_FORMAT(BITMAP_FORMAT_INDEXED16)
	MCFG_SCREEN_REFRESH_RATE(60)
	MCFG_SCREEN_SIZE(640, 400)
	MCFG_SCREEN_VISIBLE_AREA(0, 639, 0, 399)
	MCFG_SCREEN_UPDATE(generic_bitmapped)

	MCFG_PALETTE_LENGTH(2)

	MCFG_VIDEO_START(generic_bitmapped)
	MCFG_PALETTE_INIT(black_and_white)

	// sound hardware
	MCFG_SPEAKER_STANDARD_MONO("mono")
	MCFG_SOUND_ADD(YM3439_TAG, YM3439, U517/8)
	MCFG_SOUND_CONFIG(stbook_psg_intf)
	MCFG_SOUND_ROUTE(ALL_OUTPUTS, "mono", 1.00)

	// device hardware
	MCFG_ACIA6850_ADD(MC6850_0_TAG, stbook_acia_ikbd_intf)
	MCFG_ACIA6850_ADD(MC6850_1_TAG, acia_midi_intf)
	MCFG_MC68901_ADD(MC68901_TAG, U517/8, stbook_mfp_intf)
	MCFG_WD1772_ADD(WD1772_TAG, stbook_fdc_intf )
	MCFG_FLOPPY_2_DRIVES_ADD(megaste_floppy_config)
	MCFG_CENTRONICS_ADD(CENTRONICS_TAG, centronics_intf)
	MCFG_RS232_ADD(RS232_TAG, rs232_intf)

	// cartridge
	MCFG_CARTSLOT_ADD("cart")
	MCFG_CARTSLOT_EXTENSION_LIST("bin,rom")
	MCFG_CARTSLOT_NOT_MANDATORY
	MCFG_CARTSLOT_INTERFACE("st_cart")
	MCFG_SOFTWARE_LIST_ADD("cart_list", "st")

	/* internal ram */
	MCFG_RAM_ADD(RAM_TAG)
	MCFG_RAM_DEFAULT_SIZE("4M")
	MCFG_RAM_EXTRA_OPTIONS("1M")
MACHINE_CONFIG_END


//-------------------------------------------------
//  MACHINE_CONFIG( tt030 )
//-------------------------------------------------

static MACHINE_CONFIG_DERIVED( tt030, st )
MACHINE_CONFIG_END


//-------------------------------------------------
//  MACHINE_CONFIG( falcon )
//-------------------------------------------------

static MACHINE_CONFIG_DERIVED( falcon, st )
MACHINE_CONFIG_END


//-------------------------------------------------
//  MACHINE_CONFIG( falcon40 )
//-------------------------------------------------

static MACHINE_CONFIG_DERIVED( falcon40, st )
MACHINE_CONFIG_END



//**************************************************************************
//  ROMS
//**************************************************************************

//-------------------------------------------------
//  ROM( st )
//-------------------------------------------------

ROM_START( st )
	ROM_REGION16_BE( 0x30000, M68000_TAG, 0 )
	ROM_DEFAULT_BIOS("tos104")
	ROM_SYSTEM_BIOS( 0, "tos099", "TOS 0.99 (Disk TOS)" )
	ROMX_LOAD( "tos099.bin", 0x00000, 0x04000, CRC(cee3c664) SHA1(80c10b31b63b906395151204ec0a4984c8cb98d6), ROM_BIOS(1) )
	ROM_SYSTEM_BIOS( 1, "tos100", "TOS 1.0 (ROM TOS)" )
	ROMX_LOAD( "tos100.bin", 0x00000, 0x30000, BAD_DUMP CRC(d331af30) SHA1(7bcc2311d122f451bd03c9763ade5a119b2f90da), ROM_BIOS(2) )
	ROM_SYSTEM_BIOS( 2, "tos102", "TOS 1.02 (MEGA TOS)" )
	ROMX_LOAD( "tos102.bin", 0x00000, 0x30000, BAD_DUMP CRC(d3c32283) SHA1(735793fdba07fe8d5295caa03484f6ef3de931f5), ROM_BIOS(3) )
	ROM_SYSTEM_BIOS( 3, "tos104", "TOS 1.04 (Rainbow TOS)" )
	ROMX_LOAD( "tos104.bin", 0x00000, 0x30000, BAD_DUMP CRC(90f4fbff) SHA1(2487f330b0895e5d88d580d4ecb24061125e88ad), ROM_BIOS(4) )

	ROM_REGION( 0x20000, "cart", ROMREGION_ERASE00 )
	ROM_CART_LOAD( "cart", 0x00000, 0x20000, ROM_MIRROR | ROM_OPTIONAL )

	ROM_REGION( 0x1000, HD6301V1_TAG, 0 )
	ROM_LOAD( "keyboard.u1", 0x0000, 0x1000, CRC(0296915d) SHA1(1102f20d38f333234041c13687d82528b7cde2e1) )
ROM_END


//-------------------------------------------------
//  ROM( st_uk )
//-------------------------------------------------

ROM_START( st_uk )
	ROM_REGION16_BE( 0x30000, M68000_TAG, 0 )
	ROM_DEFAULT_BIOS("tos104")
	ROM_SYSTEM_BIOS( 0, "tos100", "TOS 1.0 (ROM TOS)" )
	ROMX_LOAD( "tos100uk.bin", 0x00000, 0x30000, BAD_DUMP CRC(1a586c64) SHA1(9a6e4c88533a9eaa4d55cdc040e47443e0226eb2), ROM_BIOS(1) )
	ROM_SYSTEM_BIOS( 1, "tos102", "TOS 1.02 (MEGA TOS)" )
	ROMX_LOAD( "tos102uk.bin", 0x00000, 0x30000, BAD_DUMP CRC(3b5cd0c5) SHA1(87900a40a890fdf03bd08be6c60cc645855cbce5), ROM_BIOS(2) )
	ROM_SYSTEM_BIOS( 2, "tos104", "TOS 1.04 (Rainbow TOS)" )
	ROMX_LOAD( "tos104uk.bin", 0x00000, 0x30000, BAD_DUMP CRC(a50d1d43) SHA1(9526ef63b9cb1d2a7109e278547ae78a5c1db6c6), ROM_BIOS(3) )

	ROM_REGION( 0x20000, "cart", ROMREGION_ERASE00 )
	ROM_CART_LOAD( "cart", 0x00000, 0x20000, ROM_MIRROR | ROM_OPTIONAL )

	ROM_REGION( 0x1000, HD6301V1_TAG, 0 )
	ROM_LOAD( "keyboard.u1", 0x0000, 0x1000, CRC(0296915d) SHA1(1102f20d38f333234041c13687d82528b7cde2e1) )
ROM_END


//-------------------------------------------------
//  ROM( st_de )
//-------------------------------------------------

ROM_START( st_de )
	ROM_REGION16_BE( 0x30000, M68000_TAG, 0 )
	ROM_DEFAULT_BIOS("tos104")
	ROM_SYSTEM_BIOS( 0, "tos100", "TOS 1.0 (ROM TOS)" )
	ROMX_LOAD( "tos100de.bin", 0x00000, 0x30000, BAD_DUMP CRC(16e3e979) SHA1(663d9c87cfb44ae8ada855fe9ed3cccafaa7a4ce), ROM_BIOS(1) )
	ROM_SYSTEM_BIOS( 1, "tos102", "TOS 1.02 (MEGA TOS)" )
	ROMX_LOAD( "tos102de.bin", 0x00000, 0x30000, BAD_DUMP CRC(36a0058e) SHA1(cad5d2902e875d8bf0a14dc5b5b8080b30254148), ROM_BIOS(2) )
	ROM_SYSTEM_BIOS( 2, "tos104", "TOS 1.04 (Rainbow TOS)" )
	ROMX_LOAD( "tos104de.bin", 0x00000, 0x30000, BAD_DUMP CRC(62b82b42) SHA1(5313733f91b083c6265d93674cb9d0b7efd02da8), ROM_BIOS(3) )
	ROM_SYSTEM_BIOS( 3, "tos10x", "TOS 1.0?" )
	ROMX_LOAD( "st 7c1 a4.u4", 0x00000, 0x08000, CRC(867fdd7e) SHA1(320d12acf510301e6e9ab2e3cf3ee60b0334baa0), ROM_SKIP(1) | ROM_BIOS(4) )
	ROMX_LOAD( "st 7c1 a9.u7", 0x00001, 0x08000, CRC(30e8f982) SHA1(253f26ff64b202b2681ab68ffc9954125120baea), ROM_SKIP(1) | ROM_BIOS(4) )
	ROMX_LOAD( "st 7c1 b0.u3", 0x10000, 0x08000, CRC(b91337ed) SHA1(21a338f9bbd87bce4a12d38048e03a361f58d33e), ROM_SKIP(1) | ROM_BIOS(4) )
	ROMX_LOAD( "st 7a4 a6.u6", 0x10001, 0x08000, CRC(969d7bbe) SHA1(72b998c1f25211c2a96c81a038d71b6a390585c2), ROM_SKIP(1) | ROM_BIOS(4) )
	ROMX_LOAD( "st 7c1 a2.u2", 0x20000, 0x08000, CRC(d0513329) SHA1(49855a3585e2f75b2af932dd4414ed64e6d9501f), ROM_SKIP(1) | ROM_BIOS(4) )
	ROMX_LOAD( "st 7c1 b1.u5", 0x20001, 0x08000, CRC(c115cbc8) SHA1(2b52b81a1a4e0818d63f98ee4b25c30e2eba61cb), ROM_SKIP(1) | ROM_BIOS(4) )

	ROM_REGION( 0x20000, "cart", ROMREGION_ERASE00 )
	ROM_CART_LOAD( "cart", 0x00000, 0x20000, ROM_MIRROR | ROM_OPTIONAL )

	ROM_REGION( 0x1000, HD6301V1_TAG, 0 )
	ROM_LOAD( "keyboard.u1", 0x0000, 0x1000, CRC(0296915d) SHA1(1102f20d38f333234041c13687d82528b7cde2e1) )
ROM_END


//-------------------------------------------------
//  ROM( st_fr )
//-------------------------------------------------

ROM_START( st_fr )
	ROM_REGION16_BE( 0x30000, M68000_TAG, 0 )
	ROM_DEFAULT_BIOS("tos104")
	ROM_SYSTEM_BIOS( 0, "tos100", "TOS 1.0 (ROM TOS)" )
	ROMX_LOAD( "tos100fr.bin", 0x00000, 0x30000, BAD_DUMP CRC(2b7f2117) SHA1(ecb00a2e351a6205089a281b4ce6e08959953704), ROM_BIOS(1) )
	ROM_SYSTEM_BIOS( 1, "tos102", "TOS 1.02 (MEGA TOS)" )
	ROMX_LOAD( "tos102fr.bin", 0x00000, 0x30000, BAD_DUMP CRC(8688fce6) SHA1(f5a79aac0a4e812ca77b6ac51d58d98726f331fe), ROM_BIOS(2) )
	ROM_SYSTEM_BIOS( 2, "tos104", "TOS 1.04 (Rainbow TOS)" )
	ROMX_LOAD( "tos104fr.bin", 0x00000, 0x30000, BAD_DUMP CRC(a305a404) SHA1(20dba880344b810cf63cec5066797c5a971db870), ROM_BIOS(3) )
	ROM_SYSTEM_BIOS( 3, "tos10x", "TOS 1.0?" )
	ROMX_LOAD( "c101658-001.u63", 0x00000, 0x08000, CRC(9c937f6f) SHA1(d4a3ea47568ef6233f3f2056e384b09eedd84961), ROM_SKIP(1) | ROM_BIOS(4) )
	ROMX_LOAD( "c101661-001.u67", 0x00001, 0x08000, CRC(997298f3) SHA1(9e06d42df88557252a36791b514afe455600f679), ROM_SKIP(1) | ROM_BIOS(4) )
	ROMX_LOAD( "c101657-001.u59", 0x10000, 0x08000, CRC(b63be6a1) SHA1(434f443472fc649568e4f8be6880f39c2def7819), ROM_SKIP(1) | ROM_BIOS(4) )
	ROMX_LOAD( "c101660-001.u62", 0x10001, 0x08000, CRC(a813892c) SHA1(d041c113050dfb00166c4a7a52766e1b7eac9cab), ROM_SKIP(1) | ROM_BIOS(4) )
	ROMX_LOAD( "c101656-001.u48", 0x20000, 0x08000, CRC(dbd93fb8) SHA1(cf9ec11e4bc2465490e7e6c981d9f61eae6cb359), ROM_SKIP(1) | ROM_BIOS(4) )
	ROMX_LOAD( "c101659-001.u53", 0x20001, 0x08000, CRC(67c9785a) SHA1(917a17e9f83bee015c25b327780eebb11cb2c5a5), ROM_SKIP(1) | ROM_BIOS(4) )

	ROM_REGION( 0x20000, "cart", ROMREGION_ERASE00 )
	ROM_CART_LOAD( "cart", 0x00000, 0x20000, ROM_MIRROR | ROM_OPTIONAL )

	ROM_REGION( 0x1000, HD6301V1_TAG, 0 )
	ROM_LOAD( "keyboard.u1", 0x0000, 0x1000, CRC(0296915d) SHA1(1102f20d38f333234041c13687d82528b7cde2e1) )
ROM_END


//-------------------------------------------------
//  ROM( st_es )
//-------------------------------------------------

ROM_START( st_es )
	ROM_REGION16_BE( 0x30000, M68000_TAG, 0 )
	ROM_DEFAULT_BIOS("tos104")
	ROM_SYSTEM_BIOS( 0, "tos104", "TOS 1.04 (Rainbow TOS)" )
	ROMX_LOAD( "tos104es.bin", 0x00000, 0x30000, BAD_DUMP CRC(f4e8ecd2) SHA1(df63f8ac09125d0877b55d5ba1282779b7f99c16), ROM_BIOS(1) )

	ROM_REGION( 0x20000, "cart", ROMREGION_ERASE00 )
	ROM_CART_LOAD( "cart", 0x00000, 0x20000, ROM_MIRROR | ROM_OPTIONAL )

	ROM_REGION( 0x1000, HD6301V1_TAG, 0 )
	ROM_LOAD( "keyboard.u1", 0x0000, 0x1000, CRC(0296915d) SHA1(1102f20d38f333234041c13687d82528b7cde2e1) )
ROM_END


//-------------------------------------------------
//  ROM( st_nl )
//-------------------------------------------------

ROM_START( st_nl )
	ROM_REGION16_BE( 0x30000, M68000_TAG, 0 )
	ROM_DEFAULT_BIOS("tos104")
	ROM_SYSTEM_BIOS( 0, "tos104", "TOS 1.04 (Rainbow TOS)" )
	ROMX_LOAD( "tos104nl.bin", 0x00000, 0x30000, BAD_DUMP CRC(bb4370d4) SHA1(6de7c96b2d2e5c68778f4bce3eaf85a4e121f166), ROM_BIOS(1) )

	ROM_REGION( 0x20000, "cart", ROMREGION_ERASE00 )
	ROM_CART_LOAD( "cart", 0x00000, 0x20000, ROM_MIRROR | ROM_OPTIONAL )

	ROM_REGION( 0x1000, HD6301V1_TAG, 0 )
	ROM_LOAD( "keyboard.u1", 0x0000, 0x1000, CRC(0296915d) SHA1(1102f20d38f333234041c13687d82528b7cde2e1) )
ROM_END


//-------------------------------------------------
//  ROM( st_se )
//-------------------------------------------------

ROM_START( st_se )
	ROM_REGION16_BE( 0x30000, M68000_TAG, 0 )
	ROM_DEFAULT_BIOS("tos104")
	ROM_SYSTEM_BIOS( 0, "tos102", "TOS 1.02 (MEGA TOS)" )
	ROMX_LOAD( "tos102se.bin", 0x00000, 0x30000, BAD_DUMP CRC(673fd0c2) SHA1(433de547e09576743ae9ffc43d43f2279782e127), ROM_BIOS(1) )
	ROM_SYSTEM_BIOS( 1, "tos104", "TOS 1.04 (Rainbow TOS)" )
	ROMX_LOAD( "tos104se.bin", 0x00000, 0x30000, BAD_DUMP CRC(80ecfdce) SHA1(b7ad34d5cdfbe86ea74ae79eca11dce421a7bbfd), ROM_BIOS(2) )

	ROM_REGION( 0x20000, "cart", ROMREGION_ERASE00 )
	ROM_CART_LOAD( "cart", 0x00000, 0x20000, ROM_MIRROR | ROM_OPTIONAL )

	ROM_REGION( 0x1000, HD6301V1_TAG, 0 )
	ROM_LOAD( "keyboard.u1", 0x0000, 0x1000, CRC(0296915d) SHA1(1102f20d38f333234041c13687d82528b7cde2e1) )
ROM_END


//-------------------------------------------------
//  ROM( st_sg )
//-------------------------------------------------

ROM_START( st_sg )
	ROM_REGION16_BE( 0x30000, M68000_TAG, 0 )
	ROM_DEFAULT_BIOS("tos104")
	ROM_SYSTEM_BIOS( 0, "tos102", "TOS 1.02 (MEGA TOS)" )
	ROMX_LOAD( "tos102sg.bin", 0x00000, 0x30000, BAD_DUMP CRC(5fe16c66) SHA1(45acb2fc4b1b13bd806c751aebd66c8304fc79bc), ROM_BIOS(1) )
	ROM_SYSTEM_BIOS( 1, "tos104", "TOS 1.04 (Rainbow TOS)" )
	ROMX_LOAD( "tos104sg.bin", 0x00000, 0x30000, BAD_DUMP CRC(e58f0bdf) SHA1(aa40bf7203f02b2251b9e4850a1a73ff1c7da106), ROM_BIOS(2) )

	ROM_REGION( 0x20000, "cart", ROMREGION_ERASE00 )
	ROM_CART_LOAD( "cart", 0x00000, 0x20000, ROM_MIRROR | ROM_OPTIONAL )

	ROM_REGION( 0x1000, HD6301V1_TAG, 0 )
	ROM_LOAD( "keyboard.u1", 0x0000, 0x1000, CRC(0296915d) SHA1(1102f20d38f333234041c13687d82528b7cde2e1) )
ROM_END


//-------------------------------------------------
//  ROM( megast )
//-------------------------------------------------

ROM_START( megast )
	ROM_REGION16_BE( 0x30000, M68000_TAG, 0 )
	ROM_DEFAULT_BIOS("tos104")
	ROM_SYSTEM_BIOS( 0, "tos102", "TOS 1.02 (MEGA TOS)" )
	ROMX_LOAD( "tos102.bin", 0x00000, 0x30000, BAD_DUMP CRC(d3c32283) SHA1(735793fdba07fe8d5295caa03484f6ef3de931f5), ROM_BIOS(1) )
	ROM_SYSTEM_BIOS( 1, "tos104", "TOS 1.04 (Rainbow TOS)" )
	ROMX_LOAD( "tos104.bin", 0x00000, 0x30000, BAD_DUMP CRC(90f4fbff) SHA1(2487f330b0895e5d88d580d4ecb24061125e88ad), ROM_BIOS(2) )

	ROM_REGION( 0x20000, "cart", ROMREGION_ERASE00 )
	ROM_CART_LOAD( "cart", 0x00000, 0x20000, ROM_MIRROR | ROM_OPTIONAL )

	ROM_REGION( 0x1000, HD6301V1_TAG, 0 )
	ROM_LOAD( "keyboard.u1", 0x0000, 0x1000, CRC(0296915d) SHA1(1102f20d38f333234041c13687d82528b7cde2e1) )
ROM_END


//-------------------------------------------------
//  ROM( megast_uk )
//-------------------------------------------------

ROM_START( megast_uk )
	ROM_REGION16_BE( 0x30000, M68000_TAG, 0 )
	ROM_DEFAULT_BIOS("tos104")
	ROM_SYSTEM_BIOS( 0, "tos102", "TOS 1.02 (MEGA TOS)" )
	ROMX_LOAD( "tos102uk.bin", 0x00000, 0x30000, BAD_DUMP CRC(3b5cd0c5) SHA1(87900a40a890fdf03bd08be6c60cc645855cbce5), ROM_BIOS(1) )
	ROM_SYSTEM_BIOS( 1, "tos104", "TOS 1.04 (Rainbow TOS)" )
	ROMX_LOAD( "tos104uk.bin", 0x00000, 0x30000, BAD_DUMP CRC(a50d1d43) SHA1(9526ef63b9cb1d2a7109e278547ae78a5c1db6c6), ROM_BIOS(2) )

	ROM_REGION( 0x20000, "cart", ROMREGION_ERASE00 )
	ROM_CART_LOAD( "cart", 0x00000, 0x20000, ROM_MIRROR | ROM_OPTIONAL )

	ROM_REGION( 0x1000, HD6301V1_TAG, 0 )
	ROM_LOAD( "keyboard.u1", 0x0000, 0x1000, CRC(0296915d) SHA1(1102f20d38f333234041c13687d82528b7cde2e1) )
ROM_END


//-------------------------------------------------
//  ROM( megast_de )
//-------------------------------------------------

ROM_START( megast_de )
	ROM_REGION16_BE( 0x30000, M68000_TAG, 0 )
	ROM_DEFAULT_BIOS("tos104")
	ROM_SYSTEM_BIOS( 0, "tos102", "TOS 1.02 (MEGA TOS)" )
	ROMX_LOAD( "tos102de.bin", 0x00000, 0x30000, BAD_DUMP CRC(36a0058e) SHA1(cad5d2902e875d8bf0a14dc5b5b8080b30254148), ROM_BIOS(1) )
	ROM_SYSTEM_BIOS( 1, "tos104", "TOS 1.04 (Rainbow TOS)" )
	ROMX_LOAD( "tos104de.bin", 0x00000, 0x30000, BAD_DUMP CRC(62b82b42) SHA1(5313733f91b083c6265d93674cb9d0b7efd02da8), ROM_BIOS(2) )

	ROM_REGION( 0x20000, "cart", ROMREGION_ERASE00 )
	ROM_CART_LOAD( "cart", 0x00000, 0x20000, ROM_MIRROR | ROM_OPTIONAL )

	ROM_REGION( 0x1000, HD6301V1_TAG, 0 )
	ROM_LOAD( "keyboard.u1", 0x0000, 0x1000, CRC(0296915d) SHA1(1102f20d38f333234041c13687d82528b7cde2e1) )
ROM_END


//-------------------------------------------------
//  ROM( megast_fr )
//-------------------------------------------------

ROM_START( megast_fr )
	ROM_REGION16_BE( 0x30000, M68000_TAG, 0 )
	ROM_DEFAULT_BIOS("tos104")
	ROM_SYSTEM_BIOS( 0, "tos102", "TOS 1.02 (MEGA TOS)" )
	ROMX_LOAD( "tos102fr.bin", 0x00000, 0x30000, BAD_DUMP CRC(8688fce6) SHA1(f5a79aac0a4e812ca77b6ac51d58d98726f331fe), ROM_BIOS(1) )
	ROM_SYSTEM_BIOS( 1, "tos104", "TOS 1.04 (Rainbow TOS)" )
	ROMX_LOAD( "tos104fr.bin", 0x00000, 0x30000, BAD_DUMP CRC(a305a404) SHA1(20dba880344b810cf63cec5066797c5a971db870), ROM_BIOS(2) )

	ROM_REGION( 0x20000, "cart", ROMREGION_ERASE00 )
	ROM_CART_LOAD( "cart", 0x00000, 0x20000, ROM_MIRROR | ROM_OPTIONAL )

	ROM_REGION( 0x1000, HD6301V1_TAG, 0 )
	ROM_LOAD( "keyboard.u1", 0x0000, 0x1000, CRC(0296915d) SHA1(1102f20d38f333234041c13687d82528b7cde2e1) )
ROM_END


//-------------------------------------------------
//  ROM( megast_se )
//-------------------------------------------------

ROM_START( megast_se )
	ROM_REGION16_BE( 0x30000, M68000_TAG, 0 )
	ROM_DEFAULT_BIOS("tos104")
	ROM_SYSTEM_BIOS( 0, "tos102", "TOS 1.02 (MEGA TOS)" )
	ROMX_LOAD( "tos102se.bin", 0x00000, 0x30000, BAD_DUMP CRC(673fd0c2) SHA1(433de547e09576743ae9ffc43d43f2279782e127), ROM_BIOS(1) )
	ROM_SYSTEM_BIOS( 1, "tos104", "TOS 1.04 (Rainbow TOS)" )
	ROMX_LOAD( "tos104se.bin", 0x00000, 0x30000, BAD_DUMP CRC(80ecfdce) SHA1(b7ad34d5cdfbe86ea74ae79eca11dce421a7bbfd), ROM_BIOS(2) )

	ROM_REGION( 0x20000, "cart", ROMREGION_ERASE00 )
	ROM_CART_LOAD( "cart", 0x00000, 0x20000, ROM_MIRROR | ROM_OPTIONAL )

	ROM_REGION( 0x1000, HD6301V1_TAG, 0 )
	ROM_LOAD( "keyboard.u1", 0x0000, 0x1000, CRC(0296915d) SHA1(1102f20d38f333234041c13687d82528b7cde2e1) )
ROM_END


//-------------------------------------------------
//  ROM( megast_sg )
//-------------------------------------------------

ROM_START( megast_sg )
	ROM_REGION16_BE( 0x30000, M68000_TAG, 0 )
	ROM_DEFAULT_BIOS("tos104")
	ROM_SYSTEM_BIOS( 0, "tos102", "TOS 1.02 (MEGA TOS)" )
	ROMX_LOAD( "tos102sg.bin", 0x00000, 0x30000, BAD_DUMP CRC(5fe16c66) SHA1(45acb2fc4b1b13bd806c751aebd66c8304fc79bc), ROM_BIOS(1) )
	ROM_SYSTEM_BIOS( 1, "tos104", "TOS 1.04 (Rainbow TOS)" )
	ROMX_LOAD( "tos104sg.bin", 0x00000, 0x30000, BAD_DUMP CRC(e58f0bdf) SHA1(aa40bf7203f02b2251b9e4850a1a73ff1c7da106), ROM_BIOS(2) )

	ROM_REGION( 0x20000, "cart", ROMREGION_ERASE00 )
	ROM_CART_LOAD( "cart", 0x00000, 0x20000, ROM_MIRROR | ROM_OPTIONAL )

	ROM_REGION( 0x1000, HD6301V1_TAG, 0 )
	ROM_LOAD( "keyboard.u1", 0x0000, 0x1000, CRC(0296915d) SHA1(1102f20d38f333234041c13687d82528b7cde2e1) )
ROM_END


//-------------------------------------------------
//  ROM( stacy )
//-------------------------------------------------

ROM_START( stacy )
	ROM_REGION16_BE( 0x30000, M68000_TAG, 0 )
	ROM_SYSTEM_BIOS( 0, "tos104", "TOS 1.04 (Rainbow TOS)" )
	ROMX_LOAD( "tos104.bin", 0x00000, 0x30000, BAD_DUMP CRC(a50d1d43) SHA1(9526ef63b9cb1d2a7109e278547ae78a5c1db6c6), ROM_BIOS(1) )

	ROM_REGION( 0x20000, "cart", ROMREGION_ERASE00 )
	ROM_CART_LOAD( "cart", 0x00000, 0x20000, ROM_MIRROR | ROM_OPTIONAL )

	ROM_REGION( 0x1000, HD6301V1_TAG, 0 )
	ROM_LOAD( "keyboard.u1", 0x0000, 0x1000, CRC(0296915d) SHA1(1102f20d38f333234041c13687d82528b7cde2e1) )
ROM_END


//-------------------------------------------------
//  ROM( ste )
//-------------------------------------------------

ROM_START( ste )
	ROM_REGION16_BE( 0x40000, M68000_TAG, 0 )
	ROM_DEFAULT_BIOS("tos206")
	ROM_SYSTEM_BIOS( 0, "tos106", "TOS 1.06 (STE TOS, Revision 1)" )
	ROMX_LOAD( "tos106.bin", 0x00000, 0x40000, BAD_DUMP CRC(a2e25337) SHA1(6a850810a92fdb1e64d005a06ea4079f51c97145), ROM_BIOS(1) )
	ROM_SYSTEM_BIOS( 1, "tos162", "TOS 1.62 (STE TOS, Revision 2)" )
	ROMX_LOAD( "tos162.bin", 0x00000, 0x40000, BAD_DUMP CRC(1c1a4eba) SHA1(42b875f542e5b728905d819c83c31a095a6a1904), ROM_BIOS(2) )
	ROM_SYSTEM_BIOS( 2, "tos206", "TOS 2.06 (ST/STE TOS)" )
	ROMX_LOAD( "tos206.bin", 0x00000, 0x40000, BAD_DUMP CRC(3f2f840f) SHA1(ee58768bdfc602c9b14942ce5481e97dd24e7c83), ROM_BIOS(3) )

	ROM_REGION( 0x20000, "cart", ROMREGION_ERASE00 )
	ROM_CART_LOAD( "cart", 0x00000, 0x20000, ROM_MIRROR | ROM_OPTIONAL )

	ROM_REGION( 0x1000, HD6301V1_TAG, 0 )
	ROM_LOAD( "keyboard.u1", 0x0000, 0x1000, CRC(0296915d) SHA1(1102f20d38f333234041c13687d82528b7cde2e1) )
ROM_END


//-------------------------------------------------
//  ROM( ste_uk )
//-------------------------------------------------

ROM_START( ste_uk )
	ROM_REGION16_BE( 0x40000, M68000_TAG, 0 )
	ROM_DEFAULT_BIOS("tos206")
	ROM_SYSTEM_BIOS( 0, "tos106", "TOS 1.06 (STE TOS, Revision 1)" )
	ROMX_LOAD( "tos106uk.bin", 0x00000, 0x40000, BAD_DUMP CRC(d72fea29) SHA1(06f9ea322e74b682df0396acfaee8cb4d9c90cad), ROM_BIOS(1) )
	ROM_SYSTEM_BIOS( 1, "tos162", "TOS 1.62 (STE TOS, Revision 2)" )
	ROMX_LOAD( "tos162uk.bin", 0x00000, 0x40000, BAD_DUMP CRC(d1c6f2fa) SHA1(70db24a7c252392755849f78940a41bfaebace71), ROM_BIOS(2) )
	ROM_SYSTEM_BIOS( 2, "tos206", "TOS 2.06 (ST/STE TOS)" )
	ROMX_LOAD( "tos206uk.bin", 0x00000, 0x40000, BAD_DUMP CRC(08538e39) SHA1(2400ea95f547d6ea754a99d05d8530c03f8b28e3), ROM_BIOS(3) )

	ROM_REGION( 0x20000, "cart", ROMREGION_ERASE00 )
	ROM_CART_LOAD( "cart", 0x00000, 0x20000, ROM_MIRROR | ROM_OPTIONAL )

	ROM_REGION( 0x1000, HD6301V1_TAG, 0 )
	ROM_LOAD( "keyboard.u1", 0x0000, 0x1000, CRC(0296915d) SHA1(1102f20d38f333234041c13687d82528b7cde2e1) )
ROM_END


//-------------------------------------------------
//  ROM( ste_de )
//-------------------------------------------------

ROM_START( ste_de )
	ROM_REGION16_BE( 0x40000, M68000_TAG, 0 )
	ROM_DEFAULT_BIOS("tos206")
	ROM_SYSTEM_BIOS( 0, "tos106", "TOS 1.06 (STE TOS, Revision 1)" )
	ROMX_LOAD( "tos106de.bin", 0x00000, 0x40000, BAD_DUMP CRC(7c67c5c9) SHA1(3b8cf5ffa41b252eb67f8824f94608fa4005d6dd), ROM_BIOS(1) )
	ROM_SYSTEM_BIOS( 1, "tos162", "TOS 1.62 (STE TOS, Revision 2)" )
	ROMX_LOAD( "tos162de.bin", 0x00000, 0x40000, BAD_DUMP CRC(2cdeb5e5) SHA1(10d9f61705048ee3dcbec67df741bed49b922149), ROM_BIOS(2) )
	ROM_SYSTEM_BIOS( 2, "tos206", "TOS 2.06 (ST/STE TOS)" )
	ROMX_LOAD( "tos206de.bin", 0x00000, 0x40000, BAD_DUMP CRC(143cd2ab) SHA1(d1da866560734289c4305f1028c36291d331d417), ROM_BIOS(3) )

	ROM_REGION( 0x20000, "cart", ROMREGION_ERASE00 )
	ROM_CART_LOAD( "cart", 0x00000, 0x20000, ROM_MIRROR | ROM_OPTIONAL )

	ROM_REGION( 0x1000, HD6301V1_TAG, 0 )
	ROM_LOAD( "keyboard.u1", 0x0000, 0x1000, CRC(0296915d) SHA1(1102f20d38f333234041c13687d82528b7cde2e1) )
ROM_END


//-------------------------------------------------
//  ROM( ste_es )
//-------------------------------------------------

ROM_START( ste_es )
	ROM_REGION16_BE( 0x40000, M68000_TAG, 0 )
	ROM_DEFAULT_BIOS("tos106")
	ROM_SYSTEM_BIOS( 0, "tos106", "TOS 1.06 (STE TOS, Revision 1)" )
	ROMX_LOAD( "tos106es.bin", 0x00000, 0x40000, BAD_DUMP CRC(5cd2a540) SHA1(3a18f342c8288c0bc1879b7a209c73d5d57f7e81), ROM_BIOS(1) )

	ROM_REGION( 0x20000, "cart", ROMREGION_ERASE00 )
	ROM_CART_LOAD( "cart", 0x00000, 0x20000, ROM_MIRROR | ROM_OPTIONAL )

	ROM_REGION( 0x1000, HD6301V1_TAG, 0 )
	ROM_LOAD( "keyboard.u1", 0x0000, 0x1000, CRC(0296915d) SHA1(1102f20d38f333234041c13687d82528b7cde2e1) )
ROM_END


//-------------------------------------------------
//  ROM( ste_fr )
//-------------------------------------------------

ROM_START( ste_fr )
	ROM_REGION16_BE( 0x40000, M68000_TAG, 0 )
	ROM_DEFAULT_BIOS("tos206")
	ROM_SYSTEM_BIOS( 0, "tos106", "TOS 1.06 (STE TOS, Revision 1)" )
	ROMX_LOAD( "tos106fr.bin", 0x00000, 0x40000, BAD_DUMP CRC(b6e58a46) SHA1(7d7e3cef435caa2fd7733a3fbc6930cb9ea7bcbc), ROM_BIOS(1) )
	ROM_SYSTEM_BIOS( 1, "tos162", "TOS 1.62 (STE TOS, Revision 2)" )
	ROMX_LOAD( "tos162fr.bin", 0x00000, 0x40000, BAD_DUMP CRC(0ab003be) SHA1(041e134da613f718fca8bd47cd7733076e8d7588), ROM_BIOS(2) )
	ROM_SYSTEM_BIOS( 2, "tos206", "TOS 2.06 (ST/STE TOS)" )
	ROMX_LOAD( "tos206fr.bin", 0x00000, 0x40000, BAD_DUMP CRC(e3a99ca7) SHA1(387da431e6e3dd2e0c4643207e67d06cf33618c3), ROM_BIOS(3) )

	ROM_REGION( 0x20000, "cart", ROMREGION_ERASE00 )
	ROM_CART_LOAD( "cart", 0x00000, 0x20000, ROM_MIRROR | ROM_OPTIONAL )

	ROM_REGION( 0x1000, HD6301V1_TAG, 0 )
	ROM_LOAD( "keyboard.u1", 0x0000, 0x1000, CRC(0296915d) SHA1(1102f20d38f333234041c13687d82528b7cde2e1) )
ROM_END


//-------------------------------------------------
//  ROM( ste_it )
//-------------------------------------------------

ROM_START( ste_it )
	ROM_REGION16_BE( 0x40000, M68000_TAG, 0 )
	ROM_DEFAULT_BIOS("tos106")
	ROM_SYSTEM_BIOS( 0, "tos106", "TOS 1.06 (STE TOS, Revision 1)" )
	ROMX_LOAD( "tos106it.bin", 0x00000, 0x40000, BAD_DUMP CRC(d3a55216) SHA1(28dc74e5e0fa56b685bbe15f9837f52684fee9fd), ROM_BIOS(1) )

	ROM_REGION( 0x20000, "cart", ROMREGION_ERASE00 )
	ROM_CART_LOAD( "cart", 0x00000, 0x20000, ROM_MIRROR | ROM_OPTIONAL )

	ROM_REGION( 0x1000, HD6301V1_TAG, 0 )
	ROM_LOAD( "keyboard.u1", 0x0000, 0x1000, CRC(0296915d) SHA1(1102f20d38f333234041c13687d82528b7cde2e1) )
ROM_END


//-------------------------------------------------
//  ROM( ste_se )
//-------------------------------------------------

ROM_START( ste_se )
	ROM_REGION16_BE( 0x40000, M68000_TAG, 0 )
	ROM_DEFAULT_BIOS("tos206")
	ROM_SYSTEM_BIOS( 0, "tos162", "TOS 1.62 (STE TOS, Revision 2)" )
	ROMX_LOAD( "tos162se.bin", 0x00000, 0x40000, BAD_DUMP CRC(90f124b1) SHA1(6e5454e861dbf4c46ce5020fc566c31202087b88), ROM_BIOS(1) )
	ROM_SYSTEM_BIOS( 1, "tos206", "TOS 2.06 (ST/STE TOS)" )
	ROMX_LOAD( "tos206se.bin", 0x00000, 0x40000, BAD_DUMP CRC(be61906d) SHA1(ebdf5a4cf08471cd315a91683fcb24e0f029d451), ROM_BIOS(2) )

	ROM_REGION( 0x20000, "cart", ROMREGION_ERASE00 )
	ROM_CART_LOAD( "cart", 0x00000, 0x20000, ROM_MIRROR | ROM_OPTIONAL )

	ROM_REGION( 0x1000, HD6301V1_TAG, 0 )
	ROM_LOAD( "keyboard.u1", 0x0000, 0x1000, CRC(0296915d) SHA1(1102f20d38f333234041c13687d82528b7cde2e1) )
ROM_END


//-------------------------------------------------
//  ROM( ste_sg )
//-------------------------------------------------

ROM_START( ste_sg )
	ROM_REGION16_BE( 0x40000, M68000_TAG, 0 )
	ROM_DEFAULT_BIOS("tos206")
	ROM_SYSTEM_BIOS( 0, "tos206", "TOS 2.06 (ST/STE TOS)" )
	ROMX_LOAD( "tos206sg.bin", 0x00000, 0x40000, BAD_DUMP CRC(8c4fe57d) SHA1(c7a9ae3162f020dcac0c2a46cf0c033f91b98644), ROM_BIOS(1) )

	ROM_REGION( 0x20000, "cart", ROMREGION_ERASE00 )
	ROM_CART_LOAD( "cart", 0x00000, 0x20000, ROM_MIRROR | ROM_OPTIONAL )

	ROM_REGION( 0x1000, HD6301V1_TAG, 0 )
	ROM_LOAD( "keyboard.u1", 0x0000, 0x1000, CRC(0296915d) SHA1(1102f20d38f333234041c13687d82528b7cde2e1) )
ROM_END


//-------------------------------------------------
//  ROM( megaste )
//-------------------------------------------------

ROM_START( megaste )
	ROM_REGION16_BE( 0x40000, M68000_TAG, 0 )
	ROM_DEFAULT_BIOS("tos206")
	ROM_SYSTEM_BIOS( 0, "tos205", "TOS 2.05 (Mega STE TOS)" )
	ROMX_LOAD( "tos205.bin", 0x00000, 0x40000, BAD_DUMP CRC(d8845f8d) SHA1(e069c14863819635bea33074b90c22e5bd99f1bd), ROM_BIOS(1) )
	ROM_SYSTEM_BIOS( 1, "tos206", "TOS 2.06 (ST/STE TOS)" )
	ROMX_LOAD( "tos206.bin", 0x00000, 0x40000, BAD_DUMP CRC(3f2f840f) SHA1(ee58768bdfc602c9b14942ce5481e97dd24e7c83), ROM_BIOS(2) )

	ROM_REGION( 0x20000, "cart", ROMREGION_ERASE00 )
	ROM_CART_LOAD( "cart", 0x00000, 0x20000, ROM_MIRROR | ROM_OPTIONAL )

	ROM_REGION( 0x1000, HD6301V1_TAG, 0 )
	ROM_LOAD( "keyboard.u1", 0x0000, 0x1000, CRC(0296915d) SHA1(1102f20d38f333234041c13687d82528b7cde2e1) )
ROM_END


//-------------------------------------------------
//  ROM( megaste_uk )
//-------------------------------------------------

ROM_START( megaste_uk )
	ROM_REGION16_BE( 0x40000, M68000_TAG, 0 )
	ROM_DEFAULT_BIOS("tos206")
	ROM_SYSTEM_BIOS( 0, "tos202", "TOS 2.02 (Mega STE TOS)" )
	ROMX_LOAD( "tos202uk.bin", 0x00000, 0x40000, NO_DUMP, ROM_BIOS(1) )
	ROM_SYSTEM_BIOS( 1, "tos205", "TOS 2.05 (Mega STE TOS)" )
	ROMX_LOAD( "tos205uk.bin", 0x00000, 0x40000, NO_DUMP, ROM_BIOS(2) )
	ROM_SYSTEM_BIOS( 2, "tos206", "TOS 2.06 (ST/STE TOS)" )
	ROMX_LOAD( "tos206uk.bin", 0x00000, 0x40000, BAD_DUMP CRC(08538e39) SHA1(2400ea95f547d6ea754a99d05d8530c03f8b28e3), ROM_BIOS(3) )

	ROM_REGION( 0x20000, "cart", ROMREGION_ERASE00 )
	ROM_CART_LOAD( "cart", 0x00000, 0x20000, ROM_MIRROR | ROM_OPTIONAL )

	ROM_REGION( 0x1000, HD6301V1_TAG, 0 )
	ROM_LOAD( "keyboard.u1", 0x0000, 0x1000, CRC(0296915d) SHA1(1102f20d38f333234041c13687d82528b7cde2e1) )
ROM_END


//-------------------------------------------------
//  ROM( megaste_fr )
//-------------------------------------------------

ROM_START( megaste_fr )
	ROM_REGION16_BE( 0x40000, M68000_TAG, 0 )
	ROM_DEFAULT_BIOS("tos206")
	ROM_SYSTEM_BIOS( 0, "tos205", "TOS 2.05 (Mega STE TOS)" )
	ROMX_LOAD( "tos205fr.bin", 0x00000, 0x40000, BAD_DUMP CRC(27b83d2f) SHA1(83963b0feb0d119b2ca6f51e483e8c20e6ab79e1), ROM_BIOS(1) )
	ROM_SYSTEM_BIOS( 1, "tos206", "TOS 2.06 (ST/STE TOS)" )
	ROMX_LOAD( "tos206fr.bin", 0x00000, 0x40000, BAD_DUMP CRC(e3a99ca7) SHA1(387da431e6e3dd2e0c4643207e67d06cf33618c3), ROM_BIOS(2) )

	ROM_REGION( 0x20000, "cart", ROMREGION_ERASE00 )
	ROM_CART_LOAD( "cart", 0x00000, 0x20000, ROM_MIRROR | ROM_OPTIONAL )

	ROM_REGION( 0x1000, HD6301V1_TAG, 0 )
	ROM_LOAD( "keyboard.u1", 0x0000, 0x1000, CRC(0296915d) SHA1(1102f20d38f333234041c13687d82528b7cde2e1) )
ROM_END


//-------------------------------------------------
//  ROM( megaste_de )
//-------------------------------------------------

ROM_START( megaste_de )
	ROM_REGION16_BE( 0x40000, M68000_TAG, 0 )
	ROM_DEFAULT_BIOS("tos206")
	ROM_SYSTEM_BIOS( 0, "tos205", "TOS 2.05 (Mega STE TOS)" )
	ROMX_LOAD( "tos205de.bin", 0x00000, 0x40000, BAD_DUMP CRC(518b24e6) SHA1(084e083422f8fd9ac7a2490f19b81809c52b91b4), ROM_BIOS(1) )
	ROM_SYSTEM_BIOS( 1, "tos206", "TOS 2.06 (ST/STE TOS)" )
	ROMX_LOAD( "tos206de.bin", 0x00000, 0x40000, BAD_DUMP CRC(143cd2ab) SHA1(d1da866560734289c4305f1028c36291d331d417), ROM_BIOS(2) )

	ROM_REGION( 0x20000, "cart", ROMREGION_ERASE00 )
	ROM_CART_LOAD( "cart", 0x00000, 0x20000, ROM_MIRROR | ROM_OPTIONAL )

	ROM_REGION( 0x1000, HD6301V1_TAG, 0 )
	ROM_LOAD( "keyboard.u1", 0x0000, 0x1000, CRC(0296915d) SHA1(1102f20d38f333234041c13687d82528b7cde2e1) )
ROM_END


//-------------------------------------------------
//  ROM( megaste_es )
//-------------------------------------------------

ROM_START( megaste_es )
	ROM_REGION16_BE( 0x40000, M68000_TAG, 0 )
	ROM_DEFAULT_BIOS("tos205")
	ROM_SYSTEM_BIOS( 0, "tos205", "TOS 2.05 (Mega STE TOS)" )
	ROMX_LOAD( "tos205es.bin", 0x00000, 0x40000, BAD_DUMP CRC(2a426206) SHA1(317715ad8de718b5acc7e27ecf1eb833c2017c91), ROM_BIOS(1) )

	ROM_REGION( 0x20000, "cart", ROMREGION_ERASE00 )
	ROM_CART_LOAD( "cart", 0x00000, 0x20000, ROM_MIRROR | ROM_OPTIONAL )

	ROM_REGION( 0x1000, HD6301V1_TAG, 0 )
	ROM_LOAD( "keyboard.u1", 0x0000, 0x1000, CRC(0296915d) SHA1(1102f20d38f333234041c13687d82528b7cde2e1) )
ROM_END


//-------------------------------------------------
//  ROM( megaste_it )
//-------------------------------------------------

ROM_START( megaste_it )
	ROM_REGION16_BE( 0x40000, M68000_TAG, 0 )
	ROM_DEFAULT_BIOS("tos205")
	ROM_SYSTEM_BIOS( 0, "tos205", "TOS 2.05 (Mega STE TOS)" )
	ROMX_LOAD( "tos205it.bin", 0x00000, 0x40000, BAD_DUMP CRC(b28bf5a1) SHA1(8e0581b442384af69345738849cf440d72f6e6ab), ROM_BIOS(1) )

	ROM_REGION( 0x20000, "cart", ROMREGION_ERASE00 )
	ROM_CART_LOAD( "cart", 0x00000, 0x20000, ROM_MIRROR | ROM_OPTIONAL )

	ROM_REGION( 0x1000, HD6301V1_TAG, 0 )
	ROM_LOAD( "keyboard.u1", 0x0000, 0x1000, CRC(0296915d) SHA1(1102f20d38f333234041c13687d82528b7cde2e1) )
ROM_END


//-------------------------------------------------
//  ROM( megaste_se )
//-------------------------------------------------

ROM_START( megaste_se )
	ROM_REGION16_BE( 0x40000, M68000_TAG, 0 )
	ROM_DEFAULT_BIOS("tos206")
	ROM_SYSTEM_BIOS( 0, "tos205", "TOS 2.05 (Mega STE TOS)" )
	ROMX_LOAD( "tos205se.bin", 0x00000, 0x40000, BAD_DUMP CRC(6d49ccbe) SHA1(c065b1a9a2e42e5e373333e99be829028902acaa), ROM_BIOS(1) )
	ROM_SYSTEM_BIOS( 1, "tos206", "TOS 2.06 (ST/STE TOS)" )
	ROMX_LOAD( "tos206se.bin", 0x00000, 0x40000, BAD_DUMP CRC(be61906d) SHA1(ebdf5a4cf08471cd315a91683fcb24e0f029d451), ROM_BIOS(2) )

	ROM_REGION( 0x20000, "cart", ROMREGION_ERASE00 )
	ROM_CART_LOAD( "cart", 0x00000, 0x20000, ROM_MIRROR | ROM_OPTIONAL )

	ROM_REGION( 0x1000, HD6301V1_TAG, 0 )
	ROM_LOAD( "keyboard.u1", 0x0000, 0x1000, CRC(0296915d) SHA1(1102f20d38f333234041c13687d82528b7cde2e1) )
ROM_END


//-------------------------------------------------
//  ROM( stbook )
//-------------------------------------------------

ROM_START( stbook )
	ROM_REGION16_BE( 0x40000, M68000_TAG, 0 )
	ROM_SYSTEM_BIOS( 0, "tos208", "TOS 2.08" )
	ROMX_LOAD( "tos208.bin", 0x00000, 0x40000, NO_DUMP, ROM_BIOS(1) )

	ROM_REGION( 0x20000, "cart", ROMREGION_ERASE00 )
	ROM_CART_LOAD( "cart", 0x00000, 0x20000, ROM_MIRROR | ROM_OPTIONAL )

	ROM_REGION( 0x1000, COP888_TAG, 0 )
	ROM_LOAD( "cop888c0.u703", 0x0000, 0x1000, NO_DUMP )
ROM_END


//-------------------------------------------------
//  ROM( stpad )
//-------------------------------------------------

ROM_START( stpad )
	ROM_REGION16_BE( 0x40000, M68000_TAG, 0 )
	ROM_SYSTEM_BIOS( 0, "tos205", "TOS 2.05" )
	ROMX_LOAD( "tos205.bin", 0x00000, 0x40000, NO_DUMP, ROM_BIOS(1) )

	ROM_REGION( 0x20000, "cart", ROMREGION_ERASE00 )
	ROM_CART_LOAD( "cart", 0x00000, 0x20000, ROM_MIRROR | ROM_OPTIONAL )
ROM_END


//-------------------------------------------------
//  ROM( tt030 )
//-------------------------------------------------

ROM_START( tt030 )
	ROM_REGION32_BE( 0x80000, M68000_TAG, 0 )
	ROM_DEFAULT_BIOS("tos306")
	ROM_SYSTEM_BIOS( 0, "tos306", "TOS 3.06 (TT TOS)" )
	ROMX_LOAD( "tos306.bin", 0x00000, 0x80000, BAD_DUMP CRC(e65adbd7) SHA1(b15948786278e1f2abc4effbb6d40786620acbe8), ROM_BIOS(1) )

	ROM_REGION( 0x20000, "cart", ROMREGION_ERASE00 )
	ROM_CART_LOAD( "cart", 0x00000, 0x20000, ROM_MIRROR | ROM_OPTIONAL )

	ROM_REGION( 0x1000, HD6301V1_TAG, 0 )
	ROM_LOAD( "keyboard.u1", 0x0000, 0x1000, CRC(0296915d) SHA1(1102f20d38f333234041c13687d82528b7cde2e1) )
ROM_END


//-------------------------------------------------
//  ROM( tt030_uk )
//-------------------------------------------------

ROM_START( tt030_uk )
	ROM_REGION32_BE( 0x80000, M68000_TAG, 0 )
	ROM_DEFAULT_BIOS("tos306")
	ROM_SYSTEM_BIOS( 0, "tos306", "TOS 3.06 (TT TOS)" )
	ROMX_LOAD( "tos306uk.bin", 0x00000, 0x80000, BAD_DUMP CRC(75dda215) SHA1(6325bdfd83f1b4d3afddb2b470a19428ca79478b), ROM_BIOS(1) )

	ROM_REGION( 0x20000, "cart", ROMREGION_ERASE00 )
	ROM_CART_LOAD( "cart", 0x00000, 0x20000, ROM_MIRROR | ROM_OPTIONAL )

	ROM_REGION( 0x1000, HD6301V1_TAG, 0 )
	ROM_LOAD( "keyboard.u1", 0x0000, 0x1000, CRC(0296915d) SHA1(1102f20d38f333234041c13687d82528b7cde2e1) )
ROM_END


//-------------------------------------------------
//  ROM( tt030_de )
//-------------------------------------------------

ROM_START( tt030_de )
	ROM_REGION32_BE( 0x80000, M68000_TAG, 0 )
	ROM_DEFAULT_BIOS("tos306")
	ROM_SYSTEM_BIOS( 0, "tos306", "TOS 3.06 (TT TOS)" )
	ROMX_LOAD( "tos306de.bin", 0x00000, 0x80000, BAD_DUMP CRC(4fcbb59d) SHA1(80af04499d1c3b8551fc4d72142ff02c2182e64a), ROM_BIOS(1) )

	ROM_REGION( 0x20000, "cart", ROMREGION_ERASE00 )
	ROM_CART_LOAD( "cart", 0x00000, 0x20000, ROM_MIRROR | ROM_OPTIONAL )

	ROM_REGION( 0x1000, HD6301V1_TAG, 0 )
	ROM_LOAD( "keyboard.u1", 0x0000, 0x1000, CRC(0296915d) SHA1(1102f20d38f333234041c13687d82528b7cde2e1) )
ROM_END


//-------------------------------------------------
//  ROM( tt030_fr )
//-------------------------------------------------

ROM_START( tt030_fr )
	ROM_REGION32_BE( 0x80000, M68000_TAG, 0 )
	ROM_DEFAULT_BIOS("tos306")
	ROM_SYSTEM_BIOS( 0, "tos306", "TOS 3.06 (TT TOS)" )
	ROMX_LOAD( "tos306fr.bin", 0x00000, 0x80000, BAD_DUMP CRC(1945511c) SHA1(6bb19874e1e97dba17215d4f84b992c224a81b95), ROM_BIOS(1) )

	ROM_REGION( 0x20000, "cart", ROMREGION_ERASE00 )
	ROM_CART_LOAD( "cart", 0x00000, 0x20000, ROM_MIRROR | ROM_OPTIONAL )

	ROM_REGION( 0x1000, HD6301V1_TAG, 0 )
	ROM_LOAD( "keyboard.u1", 0x0000, 0x1000, CRC(0296915d) SHA1(1102f20d38f333234041c13687d82528b7cde2e1) )
ROM_END


//-------------------------------------------------
//  ROM( tt030_pl )
//-------------------------------------------------

ROM_START( tt030_pl )
	ROM_REGION32_BE( 0x80000, M68000_TAG, 0 )
	ROM_DEFAULT_BIOS("tos306")
	ROM_SYSTEM_BIOS( 0, "tos306", "TOS 3.06 (TT TOS)" )
	ROMX_LOAD( "tos306pl.bin", 0x00000, 0x80000, BAD_DUMP CRC(4f2404bc) SHA1(d122b8ceb202b52754ff0d442b1c81f8b4de3436), ROM_BIOS(1) )

	ROM_REGION( 0x20000, "cart", ROMREGION_ERASE00 )
	ROM_CART_LOAD( "cart", 0x00000, 0x20000, ROM_MIRROR | ROM_OPTIONAL )

	ROM_REGION( 0x1000, HD6301V1_TAG, 0 )
	ROM_LOAD( "keyboard.u1", 0x0000, 0x1000, CRC(0296915d) SHA1(1102f20d38f333234041c13687d82528b7cde2e1) )
ROM_END


//-------------------------------------------------
//  ROM( fx1 )
//-------------------------------------------------

ROM_START( fx1 )
	ROM_REGION16_BE( 0x40000, M68000_TAG, 0 )
	ROM_SYSTEM_BIOS( 0, "tos207", "TOS 2.07" )
	ROMX_LOAD( "tos207.bin", 0x00000, 0x40000, NO_DUMP, ROM_BIOS(1) )

	ROM_REGION( 0x20000, "cart", ROMREGION_ERASE00 )
	ROM_CART_LOAD( "cart", 0x00000, 0x20000, ROM_MIRROR | ROM_OPTIONAL )

	ROM_REGION( 0x1000, HD6301V1_TAG, 0 )
	ROM_LOAD( "keyboard.u1", 0x0000, 0x1000, CRC(0296915d) SHA1(1102f20d38f333234041c13687d82528b7cde2e1) )
ROM_END


//-------------------------------------------------
//  ROM( falcon30 )
//-------------------------------------------------

ROM_START( falcon30 )
	ROM_REGION32_BE( 0x80000, M68000_TAG, 0 )
	ROM_DEFAULT_BIOS("tos404")
	ROM_SYSTEM_BIOS( 0, "tos400", "TOS 4.00" )
	ROMX_LOAD( "tos400.bin", 0x00000, 0x7ffff, BAD_DUMP CRC(1fbc5396) SHA1(d74d09f11a0bf37a86ccb50c6e7f91aac4d4b11b), ROM_BIOS(1) )
	ROM_SYSTEM_BIOS( 1, "tos401", "TOS 4.01" )
	ROMX_LOAD( "tos401.bin", 0x00000, 0x80000, NO_DUMP, ROM_BIOS(2) )
	ROM_SYSTEM_BIOS( 2, "tos402", "TOS 4.02" )
	ROMX_LOAD( "tos402.bin", 0x00000, 0x80000, BAD_DUMP CRC(63f82f23) SHA1(75de588f6bbc630fa9c814f738195da23b972cc6), ROM_BIOS(3) )
	ROM_SYSTEM_BIOS( 3, "tos404", "TOS 4.04" )
	ROMX_LOAD( "tos404.bin", 0x00000, 0x80000, BAD_DUMP CRC(028b561d) SHA1(27dcdb31b0951af99023b2fb8c370d8447ba6ebc), ROM_BIOS(4) )

	ROM_REGION( 0x20000, "cart", ROMREGION_ERASE00 )
	ROM_CART_LOAD( "cart", 0x00000, 0x20000, ROM_MIRROR | ROM_OPTIONAL )

	ROM_REGION( 0x1000, HD6301V1_TAG, 0 )
	ROM_LOAD( "keyboard.u1", 0x0000, 0x1000, CRC(0296915d) SHA1(1102f20d38f333234041c13687d82528b7cde2e1) )
ROM_END


//-------------------------------------------------
//  ROM( falcon40 )
//-------------------------------------------------

ROM_START( falcon40 )
	ROM_REGION32_BE( 0x80000, M68000_TAG, 0 )
	ROM_SYSTEM_BIOS( 0, "tos492", "TOS 4.92" )
	ROMX_LOAD( "tos492.bin", 0x00000, 0x7d314, BAD_DUMP CRC(bc8e497f) SHA1(747a38042844a6b632dcd9a76d8525fccb5eb892), ROM_BIOS(2) )

	ROM_REGION( 0x20000, "cart", ROMREGION_ERASE00 )
	ROM_CART_LOAD( "cart", 0x00000, 0x20000, ROM_MIRROR | ROM_OPTIONAL )

	ROM_REGION( 0x1000, HD6301V1_TAG, 0 )
	ROM_LOAD( "keyboard.u1", 0x0000, 0x1000, CRC(0296915d) SHA1(1102f20d38f333234041c13687d82528b7cde2e1) )
ROM_END



//**************************************************************************
//  SYSTEM DRIVERS
//**************************************************************************

//    YEAR  NAME        PARENT      COMPAT  MACHINE     INPUT       INIT     COMPANY    FULLNAME                FLAGS
COMP( 1985,	st,			0,			0,		st,			st,			0,		"Atari",	"ST (USA)",				GAME_NOT_WORKING | GAME_SUPPORTS_SAVE )
COMP( 1985,	st_uk,		st,			0,		st,			st,			0,		"Atari",	"ST (UK)",				GAME_NOT_WORKING | GAME_SUPPORTS_SAVE )
COMP( 1985, st_de,		st,			0,		st,			st,			0,		"Atari",	"ST (Germany)",			GAME_NOT_WORKING | GAME_SUPPORTS_SAVE )
COMP( 1985, st_es,		st,			0,		st,			st,			0,		"Atari",	"ST (Spain)",			GAME_NOT_WORKING | GAME_SUPPORTS_SAVE )
COMP( 1985, st_fr,		st,			0,		st,			st,			0,		"Atari",	"ST (France)",			GAME_NOT_WORKING | GAME_SUPPORTS_SAVE )
COMP( 1985, st_nl,		st,			0,		st,			st,			0,		"Atari",	"ST (Netherlands)",		GAME_NOT_WORKING | GAME_SUPPORTS_SAVE )
COMP( 1985, st_se,		st,			0,		st,			st,			0,		"Atari",	"ST (Sweden)",			GAME_NOT_WORKING | GAME_SUPPORTS_SAVE )
COMP( 1985, st_sg,		st,			0,		st,			st,			0,		"Atari",	"ST (Switzerland)",		GAME_NOT_WORKING | GAME_SUPPORTS_SAVE )
COMP( 1987, megast,		st,			0,		megast,		st,			0,		"Atari",	"MEGA ST (USA)",		GAME_NOT_WORKING | GAME_SUPPORTS_SAVE )
COMP( 1987, megast_uk,	st,			0,		megast,		st,			0,		"Atari",	"MEGA ST (UK)",			GAME_NOT_WORKING | GAME_SUPPORTS_SAVE )
COMP( 1987, megast_de,	st,			0,		megast,		st,			0,		"Atari",	"MEGA ST (Germany)",	GAME_NOT_WORKING | GAME_SUPPORTS_SAVE )
COMP( 1987, megast_fr,	st,			0,		megast,		st,			0,		"Atari",	"MEGA ST (France)",		GAME_NOT_WORKING | GAME_SUPPORTS_SAVE )
COMP( 1987, megast_se,	st,			0,		megast,		st,			0,		"Atari",	"MEGA ST (Sweden)",		GAME_NOT_WORKING | GAME_SUPPORTS_SAVE )
COMP( 1987, megast_sg,	st,			0,		megast,		st,			0,		"Atari",	"MEGA ST (Switzerland)",GAME_NOT_WORKING | GAME_SUPPORTS_SAVE )
COMP( 1989, ste,		0,			0,		ste,		ste,		0,		"Atari",	"STE (USA)",			GAME_NOT_WORKING | GAME_SUPPORTS_SAVE )
COMP( 1989, ste_uk,		ste,		0,		ste,		ste,		0,		"Atari",	"STE (UK)",				GAME_NOT_WORKING | GAME_SUPPORTS_SAVE )
COMP( 1989, ste_de,		ste,		0,		ste,		ste,		0,		"Atari",	"STE (Germany)",		GAME_NOT_WORKING | GAME_SUPPORTS_SAVE )
COMP( 1989, ste_es,		ste,		0,		ste,		ste,		0,		"Atari",	"STE (Spain)",			GAME_NOT_WORKING | GAME_SUPPORTS_SAVE )
COMP( 1989, ste_fr,		ste,		0,		ste,		ste,		0,		"Atari",	"STE (France)",			GAME_NOT_WORKING | GAME_SUPPORTS_SAVE )
COMP( 1989, ste_it,		ste,		0,		ste,		ste,		0,		"Atari",	"STE (Italy)",			GAME_NOT_WORKING | GAME_SUPPORTS_SAVE )
COMP( 1989, ste_se,		ste,		0,		ste,		ste,		0,		"Atari",	"STE (Sweden)",			GAME_NOT_WORKING | GAME_SUPPORTS_SAVE )
COMP( 1989, ste_sg,		ste,		0,		ste,		ste,		0,		"Atari",	"STE (Switzerland)",	GAME_NOT_WORKING | GAME_SUPPORTS_SAVE )
COMP( 1990, stbook,		ste,		0,		stbook,		stbook,		0,		"Atari",	"STBook",				GAME_NOT_WORKING )
COMP( 1990,	tt030,		0,			0,		tt030,		tt030,		0,		"Atari",	"TT030 (USA)",			GAME_NOT_WORKING )
COMP( 1990,	tt030_uk,	tt030,		0,		tt030,		tt030,		0,		"Atari",	"TT030 (UK)",			GAME_NOT_WORKING )
COMP( 1990,	tt030_de,	tt030,		0,		tt030,		tt030,		0,		"Atari",	"TT030 (Germany)",		GAME_NOT_WORKING )
COMP( 1990,	tt030_fr,	tt030,		0,		tt030,		tt030,		0,		"Atari",	"TT030 (France)",		GAME_NOT_WORKING )
COMP( 1990,	tt030_pl,	tt030,		0,		tt030,		tt030,		0,		"Atari",	"TT030 (Poland)",		GAME_NOT_WORKING )
COMP( 1991, megaste,	ste,		0,		megaste,	st,			0,		"Atari",	"MEGA STE (USA)",		GAME_NOT_WORKING | GAME_SUPPORTS_SAVE )
COMP( 1991, megaste_uk,	ste,		0,		megaste,	st,			0,		"Atari",	"MEGA STE (UK)",		GAME_NOT_WORKING | GAME_SUPPORTS_SAVE )
COMP( 1991, megaste_de,	ste,		0,		megaste,	st,			0,		"Atari",	"MEGA STE (Germany)",	GAME_NOT_WORKING | GAME_SUPPORTS_SAVE )
COMP( 1991, megaste_es,	ste,		0,		megaste,	st,			0,		"Atari",	"MEGA STE (Spain)",		GAME_NOT_WORKING | GAME_SUPPORTS_SAVE )
COMP( 1991, megaste_fr,	ste,		0,		megaste,	st,			0,		"Atari",	"MEGA STE (France)",	GAME_NOT_WORKING | GAME_SUPPORTS_SAVE )
COMP( 1991, megaste_it,	ste,		0,		megaste,	st,			0,		"Atari",	"MEGA STE (Italy)",		GAME_NOT_WORKING | GAME_SUPPORTS_SAVE )
COMP( 1991, megaste_se,	ste,		0,		megaste,	st,			0,		"Atari",	"MEGA STE (Sweden)",	GAME_NOT_WORKING | GAME_SUPPORTS_SAVE )
COMP( 1992, falcon30,	0,			0,		falcon,		falcon,		0,		"Atari",	"Falcon030",			GAME_NOT_WORKING )
COMP( 1992, falcon40,	falcon30,	0,		falcon40,	falcon,		0,		"Atari",	"Falcon040 (prototype)",GAME_NOT_WORKING )
//COMP( 1989, stacy,    st,  0,      stacy,    stacy,    0,     "Atari", "Stacy", GAME_NOT_WORKING )
//COMP( 1991, stpad,    ste, 0,      stpad,    stpad,    0,     "Atari", "STPad (prototype)", GAME_NOT_WORKING )
//COMP( 1992, fx1,      0,        0,      falcon,   falcon,   0,     "Atari", "FX-1 (prototype)", GAME_NOT_WORKING )
