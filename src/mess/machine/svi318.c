/*
** Spectravideo SVI-318 and SVI-328
**
** Sean Young, Tomas Karlsson
**
*/

#include "emu.h"
#include "video/mc6845.h"
#include "includes/svi318.h"
#include "cpu/z80/z80.h"
#include "video/tms9928a.h"
#include "machine/i8255a.h"
#include "machine/ins8250.h"
#include "machine/wd17xx.h"
#include "machine/ctronics.h"
#include "imagedev/flopdrv.h"
#include "imagedev/cassette.h"
#include "formats/svi_cas.h"
#include "sound/dac.h"
#include "sound/ay8910.h"
#include "machine/ram.h"

enum {
	SVI_INTERNAL	= 0,
	SVI_CART    	= 1,
	SVI_EXPRAM2 	= 2,
	SVI_EXPRAM3 	= 3
};


static void svi318_set_banks(running_machine &machine);


/* Serial ports */

static INS8250_INTERRUPT( svi318_ins8250_interrupt )
{
	svi318_state *drvstate = device->machine().driver_data<svi318_state>();
	if (drvstate->m_svi.bankLow != SVI_CART)
	{
		cputag_set_input_line(device->machine(), "maincpu", 0, (state ? HOLD_LINE : CLEAR_LINE));
	}
}

static INS8250_REFRESH_CONNECT( svi318_com_refresh_connected )
{
	/* Motorola MC14412 modem */
	ins8250_handshake_in(device, UART8250_HANDSHAKE_IN_CTS|UART8250_HANDSHAKE_IN_DSR|UART8250_INPUTS_RING_INDICATOR|UART8250_INPUTS_DATA_CARRIER_DETECT);
}

const ins8250_interface svi318_ins8250_interface[2]=
{
	{
		1000000,
		svi318_ins8250_interrupt,
		NULL,
		NULL,
		svi318_com_refresh_connected
	},
	{
		3072000,
		svi318_ins8250_interrupt,
		NULL,
		NULL,
		NULL
	}
};


/* Cartridge */

static int svi318_verify_cart (UINT8 magic[2])
{
	/* read the first two bytes */
	if ( (magic[0] == 0xf3) && (magic[1] == 0x31) )
		return IMAGE_VERIFY_PASS;
	else
		return IMAGE_VERIFY_FAIL;
}

DEVICE_START( svi318_cart )
{
	svi318_state *state = device->machine().driver_data<svi318_state>();
	state->m_pcart = NULL;
	state->m_pcart_rom_size = 0;
}

DEVICE_IMAGE_LOAD( svi318_cart )
{
	svi318_state *state = image.device().machine().driver_data<svi318_state>();
	UINT8 *p = image.device().machine().region("user1")->base();
	UINT32 size;

	if (image.software_entry() == NULL)
		size = image.length();
	else
		size = image.get_software_region_length("rom");

	if (size > 0x8000)
		logerror("Cart image %s larger than expected. Please report the issue.\n", image.filename());

	if (image.software_entry() == NULL)
	{
		if (image.fread(p, size) != size)
		{
			logerror("Can't read file %s\n", image.filename());
			return IMAGE_INIT_FAIL;
		}
	}
	else
		memcpy(p, image.get_software_region("rom"), size);

	if (svi318_verify_cart(p) == IMAGE_VERIFY_FAIL)
		return IMAGE_INIT_FAIL;

	state->m_pcart = p;
	state->m_pcart_rom_size = size;

	return IMAGE_INIT_PASS;
}

DEVICE_IMAGE_UNLOAD( svi318_cart )
{
	svi318_state *state = image.device().machine().driver_data<svi318_state>();
	state->m_pcart = NULL;
	state->m_pcart_rom_size = 0;
}

/* PPI */

/*
 PPI Port A Input (address 98H)
 Bit Name     Description
  1  TA       Joystick 1, /SENSE
  2  TB       Joystick 1, EOC
  3  TC       Joystick 2, /SENSE
  4  TD       Joystick 2, EOC
  5  TRIGGER1 Joystick 1, Trigger
  6  TRIGGER2 Joystick 2, Trigger
  7  /READY   Cassette, Ready
  8  CASR     Cassette, Read data
*/

static READ8_DEVICE_HANDLER ( svi318_ppi_port_a_r )
{
	int data = 0x0f;

	if (cassette_input(device->machine().device("cassette")) > 0.0038)
		data |= 0x80;
	if (!svi318_cassette_present(device->machine(), 0))
		data |= 0x40;
	data |= input_port_read(device->machine(), "BUTTONS") & 0x30;

	return data;
}

/*
 PPI Port B Input (address 99H)
 Bit Name Description
  1  IN0  Keyboard, Column status of selected line
  2  IN1  Keyboard, Column status of selected line
  3  IN2  Keyboard, Column status of selected line
  4  IN3  Keyboard, Column status of selected line
  5  IN4  Keyboard, Column status of selected line
  6  IN5  Keyboard, Column status of selected line
  7  IN6  Keyboard, Column status of selected line
  8  IN7  Keyboard, Column status of selected line
*/

static READ8_DEVICE_HANDLER ( svi318_ppi_port_b_r )
{
	svi318_state *state = device->machine().driver_data<svi318_state>();
	int row;
	static const char *const keynames[] = {
		"LINE0", "LINE1", "LINE2", "LINE3", "LINE4", "LINE5",
		"LINE6", "LINE7", "LINE8", "LINE9", "LINE10"
	};

	row = state->m_svi.keyboard_row;
	if (row <= 10)
		return input_port_read(device->machine(), keynames[row]);

	return 0xff;
}

/*
 PPI Port C Output (address 97H)
 Bit Name   Description
  1  KB0    Keyboard, Line select 0
  2  KB1    Keyboard, Line select 1
  3  KB2    Keyboard, Line select 2
  4  KB3    Keyboard, Line select 3
  5  CASON  Cassette, Motor relay control (0=on, 1=off)
  6  CASW   Cassette, Write data
  7  CASAUD Cassette, Audio out (pulse)
  8  SOUND  Keyboard, Click sound bit (pulse)
*/

static WRITE8_DEVICE_HANDLER ( svi318_ppi_port_c_w )
{
	svi318_state *state = device->machine().driver_data<svi318_state>();
	int val;

	/* key click */
	val = (data & 0x80) ? 0x3e : 0;
	val += (data & 0x40) ? 0x3e : 0;
	dac_signed_data_w (device->machine().device("dac"), val);

	/* cassette motor on/off */
	if (svi318_cassette_present(device->machine(), 0))
	{
		cassette_change_state(
			device->machine().device("cassette"),
			(data & 0x10) ? CASSETTE_MOTOR_DISABLED : CASSETTE_MOTOR_ENABLED,
			CASSETTE_MOTOR_DISABLED);
	}

	/* cassette signal write */
	cassette_output(device->machine().device("cassette"), (data & 0x20) ? -1.0 : +1.0);

	state->m_svi.keyboard_row = data & 0x0F;
}

I8255A_INTERFACE( svi318_ppi8255_interface )
{
	DEVCB_HANDLER(svi318_ppi_port_a_r),
	DEVCB_HANDLER(svi318_ppi_port_b_r),
	DEVCB_NULL,
	DEVCB_NULL,
	DEVCB_NULL,
	DEVCB_HANDLER(svi318_ppi_port_c_w)
};

READ8_DEVICE_HANDLER( svi318_ppi_r )
{
	return i8255a_r(device, offset);
}

WRITE8_DEVICE_HANDLER( svi318_ppi_w )
{
	i8255a_w(device, offset + 2, data);
}


/* PSG */

/*
 PSG Port A Input
 Bit Name   Description
  1  FWD1   Joystick 1, Forward
  2  BACK1  Joystick 1, Back
  3  LEFT1  Joystick 1, Left
  4  RIGHT1 Joystick 1, Right
  5  FWD2   Joystick 2, Forward
  6  BACK2  Joystick 2, Back
  7  LEFT2  Joystick 2, Left
  8  RIGHT2 Joystick 2, Right
*/

READ8_HANDLER( svi318_psg_port_a_r )
{
	return input_port_read(space->machine(), "JOYSTICKS");
}

/*
 PSG Port B Output
 Bit Name    Description
  1  /CART   Memory bank 11, ROM 0000-7FFF (Cartridge /CCS1, /CCS2)
  2  /BK21   Memory bank 21, RAM 0000-7FFF
  3  /BK22   Memory bank 22, RAM 8000-FFFF
  4  /BK31   Memory bank 31, RAM 0000-7FFF
  5  /BK32   Memory bank 32, RAM 8000-7FFF
  6  CAPS    Caps-Lock diod
  7  /ROMEN0 Memory bank 12, ROM 8000-BFFF* (Cartridge /CCS3)
  8  /ROMEN1 Memory bank 12, ROM C000-FFFF* (Cartridge /CCS4)

 * The /CART signal must be active for any effect and all banks
 with RAM are disabled.
*/

WRITE8_HANDLER( svi318_psg_port_b_w )
{
	svi318_state *state = space->machine().driver_data<svi318_state>();
	if ( (state->m_svi.bank_switch ^ data) & 0x20)
		set_led_status (space->machine(), 0, !(data & 0x20) );

	state->m_svi.bank_switch = data;
	svi318_set_banks(space->machine());
}

/* Disk drives  */

static WRITE_LINE_DEVICE_HANDLER( svi_fdc_intrq_w )
{
	svi318_state *drvstate = device->machine().driver_data<svi318_state>();
	drvstate->m_fdc.irq = state;
}

static WRITE_LINE_DEVICE_HANDLER( svi_fdc_drq_w )
{
	svi318_state *drvstate = device->machine().driver_data<svi318_state>();
	drvstate->m_fdc.drq = state;
}

const wd17xx_interface svi_wd17xx_interface =
{
	DEVCB_NULL,
	DEVCB_LINE(svi_fdc_intrq_w),
	DEVCB_LINE(svi_fdc_drq_w),
	{FLOPPY_0, FLOPPY_1, NULL, NULL}
};

static WRITE8_HANDLER( svi318_fdc_drive_motor_w )
{
	svi318_state *state = space->machine().driver_data<svi318_state>();
	device_t *fdc = space->machine().device("wd179x");
	switch (data & 3)
	{
	case 1:
		wd17xx_set_drive(fdc,0);
		state->m_fdc.driveselect = 0;
		break;
	case 2:
		wd17xx_set_drive(fdc,1);
		state->m_fdc.driveselect = 1;
		break;
	}
}

static WRITE8_HANDLER( svi318_fdc_density_side_w )
{
	device_t *fdc = space->machine().device("wd179x");

	wd17xx_dden_w(fdc, BIT(data, 0));
	wd17xx_set_side(fdc, BIT(data, 1));
}

static READ8_HANDLER( svi318_fdc_irqdrq_r )
{
	svi318_state *state = space->machine().driver_data<svi318_state>();
	UINT8 result = 0;

	result |= state->m_fdc.drq << 6;
	result |= state->m_fdc.irq << 7;

	return result;
}

MC6845_UPDATE_ROW( svi806_crtc6845_update_row )
{
	svi318_state *state = device->machine().driver_data<svi318_state>();
	int i;

	for( i = 0; i < x_count; i++ )
	{
		int j;
		UINT8	data = state->m_svi.svi806_gfx[ state->m_svi.svi806_ram->u8(( ma + i ) & 0x7FF) * 16 + ra ];

		if ( i == cursor_x )
		{
			data = 0xFF;
		}

		for( j=0; j < 8; j++ )
		{
			*BITMAP_ADDR16(bitmap, y, i * 8 + j ) = TMS9928A_PALETTE_SIZE + ( ( data & 0x80 ) ? 1 : 0 );
			data = data << 1;
		}
	}
}


/* 80 column card init */
static void svi318_80col_init(running_machine &machine)
{
	svi318_state *state = machine.driver_data<svi318_state>();
	/* 2K RAM, but allocating 4KB to make banking easier */
	/* The upper 2KB will be set to FFs and will never be written to */
	state->m_svi.svi806_ram = machine.region_alloc("gfx2", 0x1000, 1, ENDIANNESS_LITTLE );
	memset( state->m_svi.svi806_ram->base(), 0x00, 0x800 );
	memset( state->m_svi.svi806_ram->base() + 0x800, 0xFF, 0x800 );
	state->m_svi.svi806_gfx = machine.region("gfx1")->base();
}


static WRITE8_HANDLER( svi806_ram_enable_w )
{
	svi318_state *state = space->machine().driver_data<svi318_state>();
	state->m_svi.svi806_ram_enabled = ( data & 0x01 );
	svi318_set_banks(space->machine());
}

VIDEO_START( svi328_806 )
{
	VIDEO_START_CALL(tms9928a);
}

SCREEN_UPDATE( svi328_806 )
{
	if (!strcmp(screen->tag(), "screen"))
	{
		SCREEN_UPDATE_CALL(tms9928a);
	}
	else if (!strcmp(screen->tag(), "svi806"))
	{
		device_t *mc6845 = screen->machine().device("crtc");
		mc6845_update(mc6845, bitmap, cliprect);
	}
	else
	{
		fatalerror("Unknown screen '%s'", screen->tag());
	}
	return 0;
}

MACHINE_RESET( svi328_806 )
{
	svi318_state *state = machine.driver_data<svi318_state>();
	MACHINE_RESET_CALL(svi318);

	svi318_80col_init(machine);
	state->m_svi.svi806_present = 1;
	svi318_set_banks(machine);

	/* Set SVI-806 80 column card palette */
	palette_set_color_rgb( machine, TMS9928A_PALETTE_SIZE, 0, 0, 0 );		/* Monochrome black */
	palette_set_color_rgb( machine, TMS9928A_PALETTE_SIZE+1, 0, 224, 0 );	/* Monochrome green */
}

/* Init functions */

void svi318_vdp_interrupt(running_machine &machine, int i)
{
	cputag_set_input_line(machine, "maincpu", 0, (i ? HOLD_LINE : CLEAR_LINE));
}


static const UINT8 cc_op[0x100] = {
 4+1,10+1, 7+1, 6+1, 4+1, 4+1, 7+1, 4+1, 4+1,11+1, 7+1, 6+1, 4+1, 4+1, 7+1, 4+1,
 8+1,10+1, 7+1, 6+1, 4+1, 4+1, 7+1, 4+1,12+1,11+1, 7+1, 6+1, 4+1, 4+1, 7+1, 4+1,
 7+1,10+1,16+1, 6+1, 4+1, 4+1, 7+1, 4+1, 7+1,11+1,16+1, 6+1, 4+1, 4+1, 7+1, 4+1,
 7+1,10+1,13+1, 6+1,11+1,11+1,10+1, 4+1, 7+1,11+1,13+1, 6+1, 4+1, 4+1, 7+1, 4+1,
 4+1, 4+1, 4+1, 4+1, 4+1, 4+1, 7+1, 4+1, 4+1, 4+1, 4+1, 4+1, 4+1, 4+1, 7+1, 4+1,
 4+1, 4+1, 4+1, 4+1, 4+1, 4+1, 7+1, 4+1, 4+1, 4+1, 4+1, 4+1, 4+1, 4+1, 7+1, 4+1,
 4+1, 4+1, 4+1, 4+1, 4+1, 4+1, 7+1, 4+1, 4+1, 4+1, 4+1, 4+1, 4+1, 4+1, 7+1, 4+1,
 7+1, 7+1, 7+1, 7+1, 7+1, 7+1, 4+1, 7+1, 4+1, 4+1, 4+1, 4+1, 4+1, 4+1, 7+1, 4+1,
 4+1, 4+1, 4+1, 4+1, 4+1, 4+1, 7+1, 4+1, 4+1, 4+1, 4+1, 4+1, 4+1, 4+1, 7+1, 4+1,
 4+1, 4+1, 4+1, 4+1, 4+1, 4+1, 7+1, 4+1, 4+1, 4+1, 4+1, 4+1, 4+1, 4+1, 7+1, 4+1,
 4+1, 4+1, 4+1, 4+1, 4+1, 4+1, 7+1, 4+1, 4+1, 4+1, 4+1, 4+1, 4+1, 4+1, 7+1, 4+1,
 4+1, 4+1, 4+1, 4+1, 4+1, 4+1, 7+1, 4+1, 4+1, 4+1, 4+1, 4+1, 4+1, 4+1, 7+1, 4+1,
 5+1,10+1,10+1,10+1,10+1,11+1, 7+1,11+1, 5+1,10+1,10+1, 0+1,10+1,17+1, 7+1,11+1,
 5+1,10+1,10+1,11+1,10+1,11+1, 7+1,11+1, 5+1, 4+1,10+1,11+1,10+1, 0+1, 7+1,11+1,
 5+1,10+1,10+1,19+1,10+1,11+1, 7+1,11+1, 5+1, 4+1,10+1, 4+1,10+1, 0+1, 7+1,11+1,
 5+1,10+1,10+1, 4+1,10+1,11+1, 7+1,11+1, 5+1, 6+1,10+1, 4+1,10+1, 0+1, 7+1,11+1
};

static const UINT8 cc_cb[0x100] = {
 8+2, 8+2, 8+2, 8+2, 8+2, 8+2,15+2, 8+2, 8+2, 8+2, 8+2, 8+2, 8+2, 8+2,15+2, 8+2,
 8+2, 8+2, 8+2, 8+2, 8+2, 8+2,15+2, 8+2, 8+2, 8+2, 8+2, 8+2, 8+2, 8+2,15+2, 8+2,
 8+2, 8+2, 8+2, 8+2, 8+2, 8+2,15+2, 8+2, 8+2, 8+2, 8+2, 8+2, 8+2, 8+2,15+2, 8+2,
 8+2, 8+2, 8+2, 8+2, 8+2, 8+2,15+2, 8+2, 8+2, 8+2, 8+2, 8+2, 8+2, 8+2,15+2, 8+2,
 8+2, 8+2, 8+2, 8+2, 8+2, 8+2,12+2, 8+2, 8+2, 8+2, 8+2, 8+2, 8+2, 8+2,12+2, 8+2,
 8+2, 8+2, 8+2, 8+2, 8+2, 8+2,12+2, 8+2, 8+2, 8+2, 8+2, 8+2, 8+2, 8+2,12+2, 8+2,
 8+2, 8+2, 8+2, 8+2, 8+2, 8+2,12+2, 8+2, 8+2, 8+2, 8+2, 8+2, 8+2, 8+2,12+2, 8+2,
 8+2, 8+2, 8+2, 8+2, 8+2, 8+2,12+2, 8+2, 8+2, 8+2, 8+2, 8+2, 8+2, 8+2,12+2, 8+2,
 8+2, 8+2, 8+2, 8+2, 8+2, 8+2,15+2, 8+2, 8+2, 8+2, 8+2, 8+2, 8+2, 8+2,15+2, 8+2,
 8+2, 8+2, 8+2, 8+2, 8+2, 8+2,15+2, 8+2, 8+2, 8+2, 8+2, 8+2, 8+2, 8+2,15+2, 8+2,
 8+2, 8+2, 8+2, 8+2, 8+2, 8+2,15+2, 8+2, 8+2, 8+2, 8+2, 8+2, 8+2, 8+2,15+2, 8+2,
 8+2, 8+2, 8+2, 8+2, 8+2, 8+2,15+2, 8+2, 8+2, 8+2, 8+2, 8+2, 8+2, 8+2,15+2, 8+2,
 8+2, 8+2, 8+2, 8+2, 8+2, 8+2,15+2, 8+2, 8+2, 8+2, 8+2, 8+2, 8+2, 8+2,15+2, 8+2,
 8+2, 8+2, 8+2, 8+2, 8+2, 8+2,15+2, 8+2, 8+2, 8+2, 8+2, 8+2, 8+2, 8+2,15+2, 8+2,
 8+2, 8+2, 8+2, 8+2, 8+2, 8+2,15+2, 8+2, 8+2, 8+2, 8+2, 8+2, 8+2, 8+2,15+2, 8+2,
 8+2, 8+2, 8+2, 8+2, 8+2, 8+2,15+2, 8+2, 8+2, 8+2, 8+2, 8+2, 8+2, 8+2,15+2, 8+2
};

static const UINT8 cc_ed[0x100] = {
 8+2, 8+2, 8+2, 8+2, 8+2, 8+2, 8+2, 8+2, 8+2, 8+2, 8+2, 8+2, 8+2, 8+2, 8+2, 8+2,
 8+2, 8+2, 8+2, 8+2, 8+2, 8+2, 8+2, 8+2, 8+2, 8+2, 8+2, 8+2, 8+2, 8+2, 8+2, 8+2,
 8+2, 8+2, 8+2, 8+2, 8+2, 8+2, 8+2, 8+2, 8+2, 8+2, 8+2, 8+2, 8+2, 8+2, 8+2, 8+2,
 8+2, 8+2, 8+2, 8+2, 8+2, 8+2, 8+2, 8+2, 8+2, 8+2, 8+2, 8+2, 8+2, 8+2, 8+2, 8+2,
12+2,12+2,15+2,20+2, 8+2,14+2, 8+2, 9+2,12+2,12+2,15+2,20+2, 8+2,14+2, 8+2, 9+2,
12+2,12+2,15+2,20+2, 8+2,14+2, 8+2, 9+2,12+2,12+2,15+2,20+2, 8+2,14+2, 8+2, 9+2,
12+2,12+2,15+2,20+2, 8+2,14+2, 8+2,18+2,12+2,12+2,15+2,20+2, 8+2,14+2, 8+2,18+2,
12+2,12+2,15+2,20+2, 8+2,14+2, 8+2, 8+2,12+2,12+2,15+2,20+2, 8+2,14+2, 8+2, 8+2,
 8+2, 8+2, 8+2, 8+2, 8+2, 8+2, 8+2, 8+2, 8+2, 8+2, 8+2, 8+2, 8+2, 8+2, 8+2, 8+2,
 8+2, 8+2, 8+2, 8+2, 8+2, 8+2, 8+2, 8+2, 8+2, 8+2, 8+2, 8+2, 8+2, 8+2, 8+2, 8+2,
16+2,16+2,16+2,16+2, 8+2, 8+2, 8+2, 8+2,16+2,16+2,16+2,16+2, 8+2, 8+2, 8+2, 8+2,
16+2,16+2,16+2,16+2, 8+2, 8+2, 8+2, 8+2,16+2,16+2,16+2,16+2, 8+2, 8+2, 8+2, 8+2,
 8+2, 8+2, 8+2, 8+2, 8+2, 8+2, 8+2, 8+2, 8+2, 8+2, 8+2, 8+2, 8+2, 8+2, 8+2, 8+2,
 8+2, 8+2, 8+2, 8+2, 8+2, 8+2, 8+2, 8+2, 8+2, 8+2, 8+2, 8+2, 8+2, 8+2, 8+2, 8+2,
 8+2, 8+2, 8+2, 8+2, 8+2, 8+2, 8+2, 8+2, 8+2, 8+2, 8+2, 8+2, 8+2, 8+2, 8+2, 8+2,
 8+2, 8+2, 8+2, 8+2, 8+2, 8+2, 8+2, 8+2, 8+2, 8+2, 8+2, 8+2, 8+2, 8+2, 8+2, 8+2
};

static const UINT8 cc_xy[0x100] = {
 4+2, 4+2, 4+2, 4+2, 4+2, 4+2, 4+2, 4+2, 4+2,15+2, 4+2, 4+2, 4+2, 4+2, 4+2, 4+2,
 4+2, 4+2, 4+2, 4+2, 4+2, 4+2, 4+2, 4+2, 4+2,15+2, 4+2, 4+2, 4+2, 4+2, 4+2, 4+2,
 4+2,14+2,20+2,10+2, 9+2, 9+2, 9+2, 4+2, 4+2,15+2,20+2,10+2, 9+2, 9+2, 9+2, 4+2,
 4+2, 4+2, 4+2, 4+2,23+2,23+2,19+2, 4+2, 4+2,15+2, 4+2, 4+2, 4+2, 4+2, 4+2, 4+2,
 4+2, 4+2, 4+2, 4+2, 9+2, 9+2,19+2, 4+2, 4+2, 4+2, 4+2, 4+2, 9+2, 9+2,19+2, 4+2,
 4+2, 4+2, 4+2, 4+2, 9+2, 9+2,19+2, 4+2, 4+2, 4+2, 4+2, 4+2, 9+2, 9+2,19+2, 4+2,
 9+2, 9+2, 9+2, 9+2, 9+2, 9+2,19+2, 9+2, 9+2, 9+2, 9+2, 9+2, 9+2, 9+2,19+2, 9+2,
19+2,19+2,19+2,19+2,19+2,19+2, 4+2,19+2, 4+2, 4+2, 4+2, 4+2, 9+2, 9+2,19+2, 4+2,
 4+2, 4+2, 4+2, 4+2, 9+2, 9+2,19+2, 4+2, 4+2, 4+2, 4+2, 4+2, 9+2, 9+2,19+2, 4+2,
 4+2, 4+2, 4+2, 4+2, 9+2, 9+2,19+2, 4+2, 4+2, 4+2, 4+2, 4+2, 9+2, 9+2,19+2, 4+2,
 4+2, 4+2, 4+2, 4+2, 9+2, 9+2,19+2, 4+2, 4+2, 4+2, 4+2, 4+2, 9+2, 9+2,19+2, 4+2,
 4+2, 4+2, 4+2, 4+2, 9+2, 9+2,19+2, 4+2, 4+2, 4+2, 4+2, 4+2, 9+2, 9+2,19+2, 4+2,
 4+2, 4+2, 4+2, 4+2, 4+2, 4+2, 4+2, 4+2, 4+2, 4+2, 4+2, 0+2, 4+2, 4+2, 4+2, 4+2,
 4+2, 4+2, 4+2, 4+2, 4+2, 4+2, 4+2, 4+2, 4+2, 4+2, 4+2, 4+2, 4+2, 4+2, 4+2, 4+2,
 4+2,14+2, 4+2,23+2, 4+2,15+2, 4+2, 4+2, 4+2, 8+2, 4+2, 4+2, 4+2, 4+2, 4+2, 4+2,
 4+2, 4+2, 4+2, 4+2, 4+2, 4+2, 4+2, 4+2, 4+2,10+2, 4+2, 4+2, 4+2, 4+2, 4+2, 4+2
};

static const UINT8 cc_xycb[0x100] = {
23+2,23+2,23+2,23+2,23+2,23+2,23+2,23+2,23+2,23+2,23+2,23+2,23+2,23+2,23+2,23+2,
23+2,23+2,23+2,23+2,23+2,23+2,23+2,23+2,23+2,23+2,23+2,23+2,23+2,23+2,23+2,23+2,
23+2,23+2,23+2,23+2,23+2,23+2,23+2,23+2,23+2,23+2,23+2,23+2,23+2,23+2,23+2,23+2,
23+2,23+2,23+2,23+2,23+2,23+2,23+2,23+2,23+2,23+2,23+2,23+2,23+2,23+2,23+2,23+2,
20+2,20+2,20+2,20+2,20+2,20+2,20+2,20+2,20+2,20+2,20+2,20+2,20+2,20+2,20+2,20+2,
20+2,20+2,20+2,20+2,20+2,20+2,20+2,20+2,20+2,20+2,20+2,20+2,20+2,20+2,20+2,20+2,
20+2,20+2,20+2,20+2,20+2,20+2,20+2,20+2,20+2,20+2,20+2,20+2,20+2,20+2,20+2,20+2,
20+2,20+2,20+2,20+2,20+2,20+2,20+2,20+2,20+2,20+2,20+2,20+2,20+2,20+2,20+2,20+2,
23+2,23+2,23+2,23+2,23+2,23+2,23+2,23+2,23+2,23+2,23+2,23+2,23+2,23+2,23+2,23+2,
23+2,23+2,23+2,23+2,23+2,23+2,23+2,23+2,23+2,23+2,23+2,23+2,23+2,23+2,23+2,23+2,
23+2,23+2,23+2,23+2,23+2,23+2,23+2,23+2,23+2,23+2,23+2,23+2,23+2,23+2,23+2,23+2,
23+2,23+2,23+2,23+2,23+2,23+2,23+2,23+2,23+2,23+2,23+2,23+2,23+2,23+2,23+2,23+2,
23+2,23+2,23+2,23+2,23+2,23+2,23+2,23+2,23+2,23+2,23+2,23+2,23+2,23+2,23+2,23+2,
23+2,23+2,23+2,23+2,23+2,23+2,23+2,23+2,23+2,23+2,23+2,23+2,23+2,23+2,23+2,23+2,
23+2,23+2,23+2,23+2,23+2,23+2,23+2,23+2,23+2,23+2,23+2,23+2,23+2,23+2,23+2,23+2,
23+2,23+2,23+2,23+2,23+2,23+2,23+2,23+2,23+2,23+2,23+2,23+2,23+2,23+2,23+2,23+2
};

/* extra cycles if jr/jp/call taken and 'interrupt latency' on rst 0-7 */
static const UINT8 cc_ex[0x100] = {
 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
 5, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,	/* DJNZ */
 5, 0, 0, 0, 0, 0, 0, 0, 5, 0, 0, 0, 0, 0, 0, 0,	/* JR NZ/JR Z */
 5, 0, 0, 0, 0, 0, 0, 0, 5, 0, 0, 0, 0, 0, 0, 0,	/* JR NC/JR C */
 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
 0, 0, 0, 0, 0, 0, 0+1, 0, 0, 0, 0, 0, 0, 0, 0, 0,
 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
 5, 5, 5, 5, 0, 0, 0, 0, 5, 5, 5, 5, 0, 0, 0, 0,	/* LDIR/CPIR/INIR/OTIR LDDR/CPDR/INDR/OTDR */
 6, 0, 0, 0, 7, 0, 0, 2, 6, 0, 0, 0, 7, 0, 0, 2,
 6, 0, 0, 0, 7, 0, 0, 2, 6, 0, 0, 0, 7, 0, 0, 2,
 6, 0, 0, 0, 7, 0, 0, 2, 6, 0, 0, 0, 7, 0, 0, 2,
 6, 0, 0, 0, 7, 0, 0, 2, 6, 0, 0, 0, 7, 0, 0, 2+1
};


DRIVER_INIT( svi318 )
{
	svi318_state *state = machine.driver_data<svi318_state>();
	/* z80 stuff */
	z80_set_cycle_tables( machine.device("maincpu"), cc_op, cc_cb, cc_ed, cc_xy, cc_xycb, cc_ex );

	memset(&state->m_svi, 0, sizeof (state->m_svi) );

	if ( ! strcmp( machine.system().name, "svi318" ) || ! strcmp( machine.system().name, "svi318n" ) )
	{
		state->m_svi.svi318 = 1;
	}

	device_set_input_line_vector(machine.device("maincpu"), 0, 0xff);

	/* memory */
	state->m_svi.empty_bank = auto_alloc_array(machine, UINT8, 0x8000);
	memset (state->m_svi.empty_bank, 0xff, 0x8000);
}

static const TMS9928a_interface svi318_tms9928a_interface =
{
	TMS99x8A,
	0x4000,
	15,
	15,
	svi318_vdp_interrupt
};

static const TMS9928a_interface svi318_tms9929a_interface =
{
	TMS9929A,
	0x4000,
	13,
	13,
	svi318_vdp_interrupt
};

MACHINE_START( svi318_ntsc )
{
	TMS9928A_configure(&svi318_tms9928a_interface);
}

MACHINE_START( svi318_pal )
{
	TMS9928A_configure(&svi318_tms9929a_interface);
}

static void svi318_load_proc(device_image_interface &image)
{
	svi318_state *state = image.device().machine().driver_data<svi318_state>();
	int size;
	int id = floppy_get_drive(&image.device());

	size = image.length();
	switch (size)
	{
	case 172032:	/* SVI-328 SSDD */
		state->m_fdc.heads[id] = 1;
		break;
	case 346112:	/* SVI-328 DSDD */
		state->m_fdc.heads[id] = 2;
		break;
	case 348160:	/* SVI-728 DSDD CP/M */
		state->m_fdc.heads[id] = 2;
		break;
	}
}

MACHINE_RESET( svi318 )
{
	svi318_state *state = machine.driver_data<svi318_state>();
	int drive;
	/* video stuff */
	TMS9928A_reset();

	state->m_svi.bank_switch = 0xff;
	svi318_set_banks(machine);

	for(drive=0;drive<2;drive++)
	{
		floppy_install_load_proc(floppy_get_device(machine, drive), svi318_load_proc);
	}
}

INTERRUPT_GEN( svi318_interrupt )
{
	int set;

	set = input_port_read(device->machine(), "CONFIG");
	TMS9928A_set_spriteslimit (set & 0x20);
	TMS9928A_interrupt(device->machine());
}

/* Memory */

WRITE8_HANDLER( svi318_writemem1 )
{
	svi318_state *state = space->machine().driver_data<svi318_state>();
	if ( state->m_svi.bankLow_read_only )
		return;

	state->m_svi.bankLow_ptr[offset] = data;
}

WRITE8_HANDLER( svi318_writemem2 )
{
	svi318_state *state = space->machine().driver_data<svi318_state>();
	if ( state->m_svi.bankHigh1_read_only)
		return;

	state->m_svi.bankHigh1_ptr[offset] = data;
}

WRITE8_HANDLER( svi318_writemem3 )
{
	svi318_state *state = space->machine().driver_data<svi318_state>();
	if ( state->m_svi.bankHigh2_read_only)
		return;

	state->m_svi.bankHigh2_ptr[offset] = data;
}

WRITE8_HANDLER( svi318_writemem4 )
{
	svi318_state *state = space->machine().driver_data<svi318_state>();
	if ( state->m_svi.svi806_ram_enabled )
	{
		if ( offset < 0x800 )
		{
			state->m_svi.svi806_ram->u8(offset) = data;
		}
	}
	else
	{
		if ( state->m_svi.bankHigh2_read_only )
			return;

		state->m_svi.bankHigh2_ptr[ 0x3000 + offset] = data;
	}
}

static void svi318_set_banks(running_machine &machine)
{
	svi318_state *state = machine.driver_data<svi318_state>();
	const UINT8 v = state->m_svi.bank_switch;
	UINT8 *ram = ram_get_ptr(machine.device(RAM_TAG));
	UINT32 ram_size = ram_get_size(machine.device(RAM_TAG));

	state->m_svi.bankLow = ( v & 1 ) ? ( ( v & 2 ) ? ( ( v & 8 ) ? SVI_INTERNAL : SVI_EXPRAM3 ) : SVI_EXPRAM2 ) : SVI_CART;
	state->m_svi.bankHigh1 = ( v & 4 ) ? ( ( v & 16 ) ? SVI_INTERNAL : SVI_EXPRAM3 ) : SVI_EXPRAM2;

	state->m_svi.bankLow_ptr = state->m_svi.empty_bank;
	state->m_svi.bankLow_read_only = 1;

	switch( state->m_svi.bankLow )
	{
	case SVI_INTERNAL:
		state->m_svi.bankLow_ptr = machine.region("maincpu")->base();
		break;
	case SVI_CART:
		if ( state->m_pcart )
		{
			state->m_svi.bankLow_ptr = state->m_pcart;
		}
		break;
	case SVI_EXPRAM2:
		if ( ram_size >= 64 * 1024 )
		{
			state->m_svi.bankLow_ptr = ram + ram_size - 64 * 1024;
			state->m_svi.bankLow_read_only = 0;
		}
		break;
	case SVI_EXPRAM3:
		if ( ram_size > 128 * 1024 )
		{
			state->m_svi.bankLow_ptr = ram + ram_size - 128 * 1024;
			state->m_svi.bankLow_read_only = 0;
		}
		break;
	}

	state->m_svi.bankHigh1_ptr = state->m_svi.bankHigh2_ptr = state->m_svi.empty_bank;
	state->m_svi.bankHigh1_read_only = state->m_svi.bankHigh2_read_only = 1;

	switch( state->m_svi.bankHigh1 )
	{
	case SVI_INTERNAL:
		if ( ram_size == 16 * 1024 )
		{
			state->m_svi.bankHigh2_ptr = ram;
			state->m_svi.bankHigh2_read_only = 0;
		}
		else
		{
			state->m_svi.bankHigh1_ptr = ram;
			state->m_svi.bankHigh1_read_only = 0;
			state->m_svi.bankHigh2_ptr = ram + 0x4000;
			state->m_svi.bankHigh2_read_only = 0;
		}
		break;
	case SVI_EXPRAM2:
		if ( ram_size > 64 * 1024 )
		{
			state->m_svi.bankHigh1_ptr = ram + ram_size - 64 * 1024 + 32 * 1024;
			state->m_svi.bankHigh1_read_only = 0;
			state->m_svi.bankHigh2_ptr = ram + ram_size - 64 * 1024 + 48 * 1024;
			state->m_svi.bankHigh2_read_only = 0;
		}
		break;
	case SVI_EXPRAM3:
		if ( ram_size > 128 * 1024 )
		{
			state->m_svi.bankHigh1_ptr = ram + ram_size - 128 * 1024 + 32 * 1024;
			state->m_svi.bankHigh1_read_only = 0;
			state->m_svi.bankHigh2_ptr = ram + ram_size - 128 * 1024 + 48 * 1024;
			state->m_svi.bankHigh2_read_only = 0;
		}
		break;
	}

	/* Check for special CART based banking */
	if ( state->m_svi.bankLow == SVI_CART && ( v & 0xc0 ) != 0xc0 )
	{
		state->m_svi.bankHigh1_ptr = state->m_svi.empty_bank;
		state->m_svi.bankHigh1_read_only = 1;
		state->m_svi.bankHigh2_ptr = state->m_svi.empty_bank;
		state->m_svi.bankHigh2_read_only = 1;
		if ( state->m_pcart && ! ( v & 0x80 ) )
		{
			state->m_svi.bankHigh2_ptr = state->m_pcart + 0x4000;
		}
		if ( state->m_pcart && ! ( v & 0x40 ) )
		{
			state->m_svi.bankHigh1_ptr = state->m_pcart;
		}
	}

	memory_set_bankptr(machine, "bank1", state->m_svi.bankLow_ptr );
	memory_set_bankptr(machine, "bank2", state->m_svi.bankHigh1_ptr );
	memory_set_bankptr(machine, "bank3", state->m_svi.bankHigh2_ptr );

	/* SVI-806 80 column card specific banking */
	if ( state->m_svi.svi806_present )
	{
		if ( state->m_svi.svi806_ram_enabled )
		{
			memory_set_bankptr(machine, "bank4", state->m_svi.svi806_ram );
		}
		else
		{
			memory_set_bankptr(machine, "bank4", state->m_svi.bankHigh2_ptr + 0x3000 );
		}
	}
}

/* Cassette */

int svi318_cassette_present(running_machine &machine, int id)
{
	device_image_interface *image = dynamic_cast<device_image_interface *>(machine.device("cassette"));

	if ( image == NULL )
		return FALSE;
	return image->exists();
}

/* External I/O */

READ8_HANDLER( svi318_io_ext_r )
{
	svi318_state *state = space->machine().driver_data<svi318_state>();
	UINT8 data = 0xff;
	device_t *device;

	if (state->m_svi.bankLow == SVI_CART)
	{
		return 0xff;
	}

	switch( offset )
	{
	case 0x12:
		device = space->machine().device("centronics");
		data = 0xfe | centronics_busy_r(device);
		break;

	case 0x20:
	case 0x21:
	case 0x22:
	case 0x23:
	case 0x24:
	case 0x25:
	case 0x26:
	case 0x27:
		device = space->machine().device("ins8250_0");
		data = ins8250_r(device, offset & 7);
		break;

	case 0x28:
	case 0x29:
	case 0x2A:
	case 0x2B:
	case 0x2C:
	case 0x2D:
	case 0x2E:
	case 0x2F:
		device = space->machine().device("ins8250_1");
		data = ins8250_r(device, offset & 7);
		break;

	case 0x30:
		device = space->machine().device("wd179x");
		data = wd17xx_status_r(device, 0);
		break;
	case 0x31:
		device = space->machine().device("wd179x");
		data = wd17xx_track_r(device, 0);
		break;
	case 0x32:
		device = space->machine().device("wd179x");
		data = wd17xx_sector_r(device, 0);
		break;
	case 0x33:
		device = space->machine().device("wd179x");
		data = wd17xx_data_r(device, 0);
		break;
	case 0x34:
		data = svi318_fdc_irqdrq_r(space, 0);
		break;
	case 0x51:
		device = space->machine().device("crtc");
		data = mc6845_register_r(device, 0);
		break;
	}

	return data;
}

WRITE8_HANDLER( svi318_io_ext_w )
{
	svi318_state *state = space->machine().driver_data<svi318_state>();
	device_t *device;

	if (state->m_svi.bankLow == SVI_CART)
	{
		return;
	}

	switch( offset )
	{
	case 0x10:
		device = space->machine().device("centronics");
		centronics_data_w(device, 0, data);
		break;

	case 0x11:
		device = space->machine().device("centronics");
		centronics_strobe_w(device, BIT(data, 0));
		break;

	case 0x20:
	case 0x21:
	case 0x22:
	case 0x23:
	case 0x24:
	case 0x25:
	case 0x26:
	case 0x27:
		device = space->machine().device("ins8250_0");
		ins8250_w(device, offset & 7, data);
		break;

	case 0x28:
	case 0x29:
	case 0x2A:
	case 0x2B:
	case 0x2C:
	case 0x2D:
	case 0x2E:
	case 0x2F:
		device = space->machine().device("ins8250_1");
		ins8250_w(device, offset & 7, data);
		break;

	case 0x30:
		device = space->machine().device("wd179x");
		wd17xx_command_w(device, 0, data);
		break;
	case 0x31:
		device = space->machine().device("wd179x");
		wd17xx_track_w(device, 0, data);
		break;
	case 0x32:
		device = space->machine().device("wd179x");
		wd17xx_sector_w(device, 0, data);
		break;
	case 0x33:
		device = space->machine().device("wd179x");
		wd17xx_data_w(device, 0, data);
		break;
	case 0x34:
		svi318_fdc_drive_motor_w(space, 0, data);
		break;
	case 0x38:
		svi318_fdc_density_side_w(space, 0, data);
		break;

	case 0x50:
		device = space->machine().device("crtc");
		mc6845_address_w(device, 0, data);
		break;
	case 0x51:
		device = space->machine().device("crtc");
		mc6845_register_w(device, 0, data);
		break;

	case 0x58:
		svi806_ram_enable_w(space, 0, data);
		break;
	}
}
