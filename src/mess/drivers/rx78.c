/************************************************************************************************************

    Gundam RX-78 (c) 1983 Bandai

    driver by Angelo Salese & Robbbert.

    TODO:
    - implement printer
    - caps lock doesn't seem quite right, I need to press it twice to have the desired effect;

    Notes:
    - BS-BASIC v1.0 have a graphic bug with the RX-78 logo, it doesn't set the read bank so all of the color
      info minus plane 1 is lost when the screen scrolls vertically. Almost certainly a btanb.
    - To stop a cmt load, press STOP + SHIFT keys

==============================================================================================================
    Summary of Monitor commands.
    - The monitor is entered at bootup. The prompt is the * character. This is followed by a command
      letter (upper case). Some commands require hex parameters. You must enter all 4 characters of
      these. No spaces allowed except where shown.
    - While in BASIC, you may enter the monitor by using the MON command. After you have finished,
      you can return to BASIC by entering the command *J2005.

    - Tape commands:
    *L Load a tape
    *V Verify a tape
    *S Save a block of memory to tape. You are asked for a filename (blank is allowed), the start address,
       the end address, and the Jump address (where it should begin execution)

    - Memory commands:
    *Dnnnn nnnn Displays a hex dump in the address range entered
    *Mnnnn      Allows you to examine and modify memory. Enter to skip to next, period (.) to quit.
    *Jnnnn      Transfer execution (Jump) to a program in memory at the specified address

    - Other:
    *R          This is a block transfer load from a mystery parallel device, using ports E0 and E1,
                using handshaking similar to a centronics printer. The incoming file is loaded into
                memory and it appears that the operator is not provided any information of what happened.

*************************************************************************************************************/

#include "emu.h"
#include "cpu/z80/z80.h"
#include "sound/sn76496.h"
#include "imagedev/cartslot.h"
#include "imagedev/cassette.h"
#include "sound/wave.h"
#include "machine/ram.h"


class rx78_state : public driver_device
{
public:
	rx78_state(running_machine &machine, const driver_device_config_base &config)
		: driver_device(machine, config) { }

	device_t *m_cassette;
	UINT8 m_vram_read_bank;
	UINT8 m_vram_write_bank;
	UINT8 m_pal_reg[7];
	UINT8 m_pri_mask;
	UINT8 m_key_mux;
};


#define MASTER_CLOCK XTAL_28_63636MHz


static WRITE8_HANDLER( rx78_f0_w )
{
	rx78_state *state = space->machine().driver_data<rx78_state>();
	cassette_output(state->m_cassette, (data & 1) ? -1.0 : +1.0);
}

static READ8_HANDLER( rx78_f0_r )
{
	rx78_state *state = space->machine().driver_data<rx78_state>();
	UINT8 data = 0;

	if (cassette_input(state->m_cassette) > 0.03)
		data++;

	return data;
}



static VIDEO_START( rx78 )
{
}

static SCREEN_UPDATE( rx78 )
{
	rx78_state *state = screen->machine().driver_data<rx78_state>();
	UINT8 *vram = screen->machine().region("vram")->base();
	int x,y,count;

	bitmap_fill(bitmap, cliprect, screen->machine().pens[0x10]);

	count = 0x2c0; //first 0x2bf bytes aren't used for bitmap drawing apparently

	for(y=0;y<184;y++)
	{
		for(x=0;x<192;x+=8)
		{
			int color,pen[3],i;

			for (i = 0; i < 8; i++)
			{
				/* bg color */
				pen[0] = (state->m_pri_mask & 0x08) ? (vram[count + 0x6000] >> (i)) : 0x00;
				pen[1] = (state->m_pri_mask & 0x10) ? (vram[count + 0x8000] >> (i)) : 0x00;
				pen[2] = (state->m_pri_mask & 0x20) ? (vram[count + 0xa000] >> (i)) : 0x00;

				color  = ((pen[0] & 1) << 0);
				color |= ((pen[1] & 1) << 1);
				color |= ((pen[2] & 1) << 2);

				if(color)
					*BITMAP_ADDR16(bitmap, y, x+i) = screen->machine().pens[color];

				/* fg color */
				pen[0] = (state->m_pri_mask & 0x01) ? (vram[count + 0x0000] >> (i)) : 0x00;
				pen[1] = (state->m_pri_mask & 0x02) ? (vram[count + 0x2000] >> (i)) : 0x00;
				pen[2] = (state->m_pri_mask & 0x04) ? (vram[count + 0x4000] >> (i)) : 0x00;

				color  = ((pen[0] & 1) << 0);
				color |= ((pen[1] & 1) << 1);
				color |= ((pen[2] & 1) << 2);

				if(color)
					*BITMAP_ADDR16(bitmap, y, x+i) = screen->machine().pens[color];
			}
			count++;
		}
	}

	return 0;
}


static READ8_HANDLER( key_r )
{
	rx78_state *state = space->machine().driver_data<rx78_state>();
	static const char *const keynames[] = { "KEY0", "KEY1", "KEY2", "KEY3",
	                                        "KEY4", "KEY5", "KEY6", "KEY7",
	                                        "KEY8", "JOY1P_0","JOY1P_1","JOY1P_2",
	                                        "JOY2P_0", "JOY2P_1", "JOY2P_2", "UNUSED" };

	if(state->m_key_mux == 0x30) //status read
	{
		int res,i;

		res = 0;
		for(i=0;i<15;i++)
			res |= input_port_read(space->machine(), keynames[i]);

		return res;
	}

	if(state->m_key_mux >= 1 && state->m_key_mux <= 15)
		return input_port_read(space->machine(), keynames[state->m_key_mux - 1]);

	return 0;
}

static WRITE8_HANDLER( key_w )
{
	rx78_state *state = space->machine().driver_data<rx78_state>();
	state->m_key_mux = data;
}

static READ8_HANDLER( rx78_vram_r )
{
	rx78_state *state = space->machine().driver_data<rx78_state>();
	UINT8 *vram = space->machine().region("vram")->base();

	if(state->m_vram_read_bank == 0 || state->m_vram_read_bank > 6)
		return 0xff;

	return vram[offset + ((state->m_vram_read_bank - 1) * 0x2000)];
}

static WRITE8_HANDLER( rx78_vram_w )
{
	rx78_state *state = space->machine().driver_data<rx78_state>();
	UINT8 *vram = space->machine().region("vram")->base();

	if(state->m_vram_write_bank & 0x01) { vram[offset + 0 * 0x2000] = data; }
	if(state->m_vram_write_bank & 0x02) { vram[offset + 1 * 0x2000] = data; }
	if(state->m_vram_write_bank & 0x04) { vram[offset + 2 * 0x2000] = data; }
	if(state->m_vram_write_bank & 0x08) { vram[offset + 3 * 0x2000] = data; }
	if(state->m_vram_write_bank & 0x10) { vram[offset + 4 * 0x2000] = data; }
	if(state->m_vram_write_bank & 0x20) { vram[offset + 5 * 0x2000] = data; }
}

static WRITE8_HANDLER( vram_read_bank_w )
{
	rx78_state *state = space->machine().driver_data<rx78_state>();
	state->m_vram_read_bank = data;
}

static WRITE8_HANDLER( vram_write_bank_w )
{
	rx78_state *state = space->machine().driver_data<rx78_state>();
	state->m_vram_write_bank = data;
}

static WRITE8_HANDLER( vdp_reg_w )
{
	rx78_state *state = space->machine().driver_data<rx78_state>();
	UINT8 r,g,b,res,i;

	state->m_pal_reg[offset] = data;

	for(i=0;i<16;i++)
	{
		res = ((i & 1) ? state->m_pal_reg[0 + (i & 8 ? 3 : 0)] : 0) | ((i & 2) ? state->m_pal_reg[1 + (i & 8 ? 3 : 0)] : 0) | ((i & 4) ? state->m_pal_reg[2 + (i & 8 ? 3 : 0)] : 0);
		if(res & state->m_pal_reg[6]) //color mask, TODO: check this
			res &= state->m_pal_reg[6];

		r = (res & 0x11) == 0x11 ? 0xff : ((res & 0x11) == 0x01 ? 0x7f : 0x00);
		g = (res & 0x22) == 0x22 ? 0xff : ((res & 0x22) == 0x02 ? 0x7f : 0x00);
		b = (res & 0x44) == 0x44 ? 0xff : ((res & 0x44) == 0x04 ? 0x7f : 0x00);

		palette_set_color(space->machine(), i, MAKE_RGB(r,g,b));
	}
}

static WRITE8_HANDLER( vdp_bg_reg_w )
{
	int r,g,b;

	r = (data & 0x11) == 0x11 ? 0xff : ((data & 0x11) == 0x01 ? 0x7f : 0x00);
	g = (data & 0x22) == 0x22 ? 0xff : ((data & 0x22) == 0x02 ? 0x7f : 0x00);
	b = (data & 0x44) == 0x44 ? 0xff : ((data & 0x44) == 0x04 ? 0x7f : 0x00);

	palette_set_color(space->machine(), 0x10, MAKE_RGB(r,g,b));
}

static WRITE8_HANDLER( vdp_pri_mask_w )
{
	rx78_state *state = space->machine().driver_data<rx78_state>();
	state->m_pri_mask = data;
}


static ADDRESS_MAP_START(rx78_mem, AS_PROGRAM, 8)
	ADDRESS_MAP_UNMAP_HIGH
	AM_RANGE(0x0000, 0x1fff) AM_ROM
	AM_RANGE(0x2000, 0x5fff) AM_ROM AM_REGION("cart_img", 0x0000)
	AM_RANGE(0x6000, 0xafff) AM_RAM //ext RAM
	AM_RANGE(0xb000, 0xebff) AM_RAM
	AM_RANGE(0xec00, 0xffff) AM_READWRITE(rx78_vram_r, rx78_vram_w)
ADDRESS_MAP_END

static ADDRESS_MAP_START( rx78_io , AS_IO, 8)
	ADDRESS_MAP_UNMAP_HIGH
	ADDRESS_MAP_GLOBAL_MASK(0xff)
//  AM_RANGE(0xe2, 0xe2) AM_READNOP AM_WRITENOP //printer
//  AM_RANGE(0xe3, 0xe3) AM_WRITENOP //printer
	AM_RANGE(0xf0, 0xf0) AM_READWRITE(rx78_f0_r,rx78_f0_w) //cmt
	AM_RANGE(0xf1, 0xf1) AM_WRITE(vram_read_bank_w)
	AM_RANGE(0xf2, 0xf2) AM_WRITE(vram_write_bank_w)
	AM_RANGE(0xf4, 0xf4) AM_READWRITE(key_r,key_w) //keyboard
	AM_RANGE(0xf5, 0xfb) AM_WRITE(vdp_reg_w) //vdp
	AM_RANGE(0xfc, 0xfc) AM_WRITE(vdp_bg_reg_w) //vdp
	AM_RANGE(0xfe, 0xfe) AM_WRITE(vdp_pri_mask_w)
	AM_RANGE(0xff, 0xff) AM_DEVWRITE("sn1",sn76496_w) //psg
ADDRESS_MAP_END

/* Input ports */
static INPUT_PORTS_START( rx78 )
	PORT_START("KEY0")
	PORT_BIT(0x01,IP_ACTIVE_HIGH,IPT_KEYBOARD) PORT_NAME("0") PORT_CODE(KEYCODE_0) PORT_CHAR('0')
	PORT_BIT(0x02,IP_ACTIVE_HIGH,IPT_KEYBOARD) PORT_NAME("1") PORT_CODE(KEYCODE_1) PORT_CHAR('1')
	PORT_BIT(0x04,IP_ACTIVE_HIGH,IPT_KEYBOARD) PORT_NAME("2") PORT_CODE(KEYCODE_2) PORT_CHAR('2')
	PORT_BIT(0x08,IP_ACTIVE_HIGH,IPT_KEYBOARD) PORT_NAME("3") PORT_CODE(KEYCODE_3) PORT_CHAR('3')
	PORT_BIT(0x10,IP_ACTIVE_HIGH,IPT_KEYBOARD) PORT_NAME("4") PORT_CODE(KEYCODE_4) PORT_CHAR('4')
	PORT_BIT(0x20,IP_ACTIVE_HIGH,IPT_KEYBOARD) PORT_NAME("5") PORT_CODE(KEYCODE_5) PORT_CHAR('5')
	PORT_BIT(0x40,IP_ACTIVE_HIGH,IPT_KEYBOARD) PORT_NAME("6") PORT_CODE(KEYCODE_6) PORT_CHAR('6')
	PORT_BIT(0x80,IP_ACTIVE_HIGH,IPT_KEYBOARD) PORT_NAME("7") PORT_CODE(KEYCODE_7) PORT_CHAR('7')

	PORT_START("KEY1")
	PORT_BIT(0x01,IP_ACTIVE_HIGH,IPT_KEYBOARD) PORT_NAME("8") PORT_CODE(KEYCODE_8) PORT_CHAR('8')
	PORT_BIT(0x02,IP_ACTIVE_HIGH,IPT_KEYBOARD) PORT_NAME("9") PORT_CODE(KEYCODE_9) PORT_CHAR('9')
	PORT_BIT(0x04,IP_ACTIVE_HIGH,IPT_KEYBOARD) PORT_NAME(":") PORT_CODE(KEYCODE_QUOTE) PORT_CHAR(':')
	PORT_BIT(0x08,IP_ACTIVE_HIGH,IPT_KEYBOARD) PORT_NAME(";") PORT_CODE(KEYCODE_COLON) PORT_CHAR(';')
	PORT_BIT(0x10,IP_ACTIVE_HIGH,IPT_KEYBOARD) PORT_NAME(",") PORT_CODE(KEYCODE_COMMA) PORT_CHAR(',')
	PORT_BIT(0x20,IP_ACTIVE_HIGH,IPT_KEYBOARD) PORT_NAME("-") PORT_CODE(KEYCODE_MINUS) PORT_CHAR('-')
	PORT_BIT(0x40,IP_ACTIVE_HIGH,IPT_KEYBOARD) PORT_NAME(".") PORT_CODE(KEYCODE_STOP) PORT_CHAR('.')
	PORT_BIT(0x80,IP_ACTIVE_HIGH,IPT_KEYBOARD) PORT_NAME("/") PORT_CODE(KEYCODE_SLASH) PORT_CHAR('/')

	PORT_START("KEY2")
	PORT_BIT(0x01,IP_ACTIVE_HIGH,IPT_KEYBOARD) PORT_NAME("@") PORT_CODE(KEYCODE_OPENBRACE) PORT_CHAR('@')
	PORT_BIT(0x02,IP_ACTIVE_HIGH,IPT_KEYBOARD) PORT_NAME("A") PORT_CODE(KEYCODE_A) PORT_CHAR('A')
	PORT_BIT(0x04,IP_ACTIVE_HIGH,IPT_KEYBOARD) PORT_NAME("B") PORT_CODE(KEYCODE_B) PORT_CHAR('B')
	PORT_BIT(0x08,IP_ACTIVE_HIGH,IPT_KEYBOARD) PORT_NAME("C") PORT_CODE(KEYCODE_C) PORT_CHAR('C')
	PORT_BIT(0x10,IP_ACTIVE_HIGH,IPT_KEYBOARD) PORT_NAME("D") PORT_CODE(KEYCODE_D) PORT_CHAR('D')
	PORT_BIT(0x20,IP_ACTIVE_HIGH,IPT_KEYBOARD) PORT_NAME("E") PORT_CODE(KEYCODE_E) PORT_CHAR('E')
	PORT_BIT(0x40,IP_ACTIVE_HIGH,IPT_KEYBOARD) PORT_NAME("F") PORT_CODE(KEYCODE_F) PORT_CHAR('F')
	PORT_BIT(0x80,IP_ACTIVE_HIGH,IPT_KEYBOARD) PORT_NAME("G") PORT_CODE(KEYCODE_G) PORT_CHAR('G')

	PORT_START("KEY3")
	PORT_BIT(0x01,IP_ACTIVE_HIGH,IPT_KEYBOARD) PORT_NAME("H") PORT_CODE(KEYCODE_H) PORT_CHAR('H')
	PORT_BIT(0x02,IP_ACTIVE_HIGH,IPT_KEYBOARD) PORT_NAME("I") PORT_CODE(KEYCODE_I) PORT_CHAR('I')
	PORT_BIT(0x04,IP_ACTIVE_HIGH,IPT_KEYBOARD) PORT_NAME("J") PORT_CODE(KEYCODE_J) PORT_CHAR('J')
	PORT_BIT(0x08,IP_ACTIVE_HIGH,IPT_KEYBOARD) PORT_NAME("K") PORT_CODE(KEYCODE_K) PORT_CHAR('K')
	PORT_BIT(0x10,IP_ACTIVE_HIGH,IPT_KEYBOARD) PORT_NAME("L") PORT_CODE(KEYCODE_L) PORT_CHAR('L')
	PORT_BIT(0x20,IP_ACTIVE_HIGH,IPT_KEYBOARD) PORT_NAME("M") PORT_CODE(KEYCODE_M) PORT_CHAR('M')
	PORT_BIT(0x40,IP_ACTIVE_HIGH,IPT_KEYBOARD) PORT_NAME("N") PORT_CODE(KEYCODE_N) PORT_CHAR('N')
	PORT_BIT(0x80,IP_ACTIVE_HIGH,IPT_KEYBOARD) PORT_NAME("O") PORT_CODE(KEYCODE_O) PORT_CHAR('O')

	PORT_START("KEY4")
	PORT_BIT(0x01,IP_ACTIVE_HIGH,IPT_KEYBOARD) PORT_NAME("P") PORT_CODE(KEYCODE_P) PORT_CHAR('P')
	PORT_BIT(0x02,IP_ACTIVE_HIGH,IPT_KEYBOARD) PORT_NAME("Q") PORT_CODE(KEYCODE_Q) PORT_CHAR('Q')
	PORT_BIT(0x04,IP_ACTIVE_HIGH,IPT_KEYBOARD) PORT_NAME("R") PORT_CODE(KEYCODE_R) PORT_CHAR('R')
	PORT_BIT(0x08,IP_ACTIVE_HIGH,IPT_KEYBOARD) PORT_NAME("S") PORT_CODE(KEYCODE_S) PORT_CHAR('S')
	PORT_BIT(0x10,IP_ACTIVE_HIGH,IPT_KEYBOARD) PORT_NAME("T") PORT_CODE(KEYCODE_T) PORT_CHAR('T')
	PORT_BIT(0x20,IP_ACTIVE_HIGH,IPT_KEYBOARD) PORT_NAME("U") PORT_CODE(KEYCODE_U) PORT_CHAR('U')
	PORT_BIT(0x40,IP_ACTIVE_HIGH,IPT_KEYBOARD) PORT_NAME("V") PORT_CODE(KEYCODE_V) PORT_CHAR('V')
	PORT_BIT(0x80,IP_ACTIVE_HIGH,IPT_KEYBOARD) PORT_NAME("W") PORT_CODE(KEYCODE_W) PORT_CHAR('W')

	PORT_START("KEY5")
	PORT_BIT(0x01,IP_ACTIVE_HIGH,IPT_KEYBOARD) PORT_NAME("X") PORT_CODE(KEYCODE_X) PORT_CHAR('X')
	PORT_BIT(0x02,IP_ACTIVE_HIGH,IPT_KEYBOARD) PORT_NAME("Y") PORT_CODE(KEYCODE_Y) PORT_CHAR('Y')
	PORT_BIT(0x04,IP_ACTIVE_HIGH,IPT_KEYBOARD) PORT_NAME("Z") PORT_CODE(KEYCODE_Z) PORT_CHAR('Z')
	PORT_BIT(0x08,IP_ACTIVE_HIGH,IPT_KEYBOARD) PORT_NAME("[") PORT_CODE(KEYCODE_OPENBRACE) PORT_CHAR('[')
	PORT_BIT(0x10,IP_ACTIVE_HIGH,IPT_KEYBOARD) PORT_NAME("\\") PORT_CODE(KEYCODE_BACKSLASH)
	PORT_BIT(0x20,IP_ACTIVE_HIGH,IPT_KEYBOARD) PORT_NAME("]") PORT_CODE(KEYCODE_CLOSEBRACE) PORT_CHAR(']')
	PORT_BIT(0x40,IP_ACTIVE_HIGH,IPT_KEYBOARD) PORT_NAME("Up Down Arrow") //???
	PORT_BIT(0x80,IP_ACTIVE_HIGH,IPT_KEYBOARD) PORT_NAME("Right Left Arrow") //???

	PORT_START("KEY6")
	PORT_BIT(0x01,IP_ACTIVE_HIGH,IPT_KEYBOARD) PORT_NAME("Space") PORT_CODE(KEYCODE_SPACE) PORT_CHAR(' ')
	PORT_BIT(0x02,IP_ACTIVE_HIGH,IPT_KEYBOARD) PORT_NAME("Down") PORT_CODE(KEYCODE_DOWN)
	PORT_BIT(0x04,IP_ACTIVE_HIGH,IPT_KEYBOARD) PORT_NAME("Up") PORT_CODE(KEYCODE_UP)
	PORT_BIT(0x08,IP_ACTIVE_HIGH,IPT_KEYBOARD) PORT_NAME("Right") PORT_CODE(KEYCODE_RIGHT)
	PORT_BIT(0x10,IP_ACTIVE_HIGH,IPT_KEYBOARD) PORT_NAME("Left") PORT_CODE(KEYCODE_LEFT)
	PORT_BIT(0x20,IP_ACTIVE_HIGH,IPT_KEYBOARD) PORT_NAME("HOME") PORT_CODE(KEYCODE_HOME)
	PORT_BIT(0x40,IP_ACTIVE_HIGH,IPT_UNUSED )
	PORT_BIT(0x80,IP_ACTIVE_HIGH,IPT_KEYBOARD) PORT_NAME("INST / DEL") PORT_CODE(KEYCODE_BACKSPACE)

	PORT_START("KEY7")
	PORT_BIT(0x07,IP_ACTIVE_HIGH,IPT_UNUSED )
	PORT_BIT(0x08,IP_ACTIVE_HIGH,IPT_KEYBOARD) PORT_NAME("STOP")
	PORT_BIT(0x10,IP_ACTIVE_HIGH,IPT_UNUSED )
	PORT_BIT(0x20,IP_ACTIVE_HIGH,IPT_KEYBOARD) PORT_NAME("RETURN") PORT_CODE(KEYCODE_ENTER) PORT_CHAR(27)
	PORT_BIT(0x40,IP_ACTIVE_HIGH,IPT_UNUSED )
	PORT_BIT(0x80,IP_ACTIVE_HIGH,IPT_KEYBOARD) PORT_NAME("SHIFT LOCK") PORT_CODE(KEYCODE_CAPSLOCK) PORT_TOGGLE

	PORT_START("KEY8")
	PORT_BIT(0x01,IP_ACTIVE_HIGH,IPT_KEYBOARD) PORT_NAME("CTRL") PORT_CODE(KEYCODE_LCONTROL) //kana shift?
	PORT_BIT(0x02,IP_ACTIVE_HIGH,IPT_UNUSED )
	PORT_BIT(0x04,IP_ACTIVE_HIGH,IPT_KEYBOARD) PORT_NAME("SHIFT") PORT_CODE(KEYCODE_LSHIFT)
	PORT_BIT(0xf8,IP_ACTIVE_HIGH,IPT_UNUSED )

	PORT_START("JOY1P_0")
	PORT_BIT( 0x11, IP_ACTIVE_HIGH, IPT_JOYSTICK_UP ) PORT_8WAY PORT_PLAYER(1)
	PORT_BIT( 0x22, IP_ACTIVE_HIGH, IPT_OTHER ) PORT_NAME("P1 Up Left") PORT_PLAYER(1)
	PORT_BIT( 0x44, IP_ACTIVE_HIGH, IPT_JOYSTICK_LEFT ) PORT_8WAY PORT_PLAYER(1)
	PORT_BIT( 0x88, IP_ACTIVE_HIGH, IPT_BUTTON1 ) PORT_PLAYER(1)

	PORT_START("JOY1P_1")
	PORT_BIT( 0x11, IP_ACTIVE_HIGH, IPT_OTHER ) PORT_NAME("P1 Down Left")  PORT_PLAYER(1)
	PORT_BIT( 0x22, IP_ACTIVE_HIGH, IPT_OTHER ) PORT_NAME("P1 Up Right") PORT_PLAYER(1)
	PORT_BIT( 0x44, IP_ACTIVE_HIGH, IPT_UNUSED )
	PORT_BIT( 0x88, IP_ACTIVE_HIGH, IPT_UNUSED )

	PORT_START("JOY1P_2")
	PORT_BIT( 0x11, IP_ACTIVE_HIGH, IPT_JOYSTICK_DOWN ) PORT_8WAY  PORT_PLAYER(1)
	PORT_BIT( 0x22, IP_ACTIVE_HIGH, IPT_OTHER ) PORT_NAME("P1 Down Right") PORT_PLAYER(1)
	PORT_BIT( 0x44, IP_ACTIVE_HIGH, IPT_JOYSTICK_RIGHT ) PORT_8WAY  PORT_PLAYER(1)
	PORT_BIT( 0x88, IP_ACTIVE_HIGH, IPT_BUTTON2 ) PORT_PLAYER(1)

	PORT_START("JOY2P_0")
	PORT_BIT( 0x11, IP_ACTIVE_HIGH, IPT_JOYSTICK_UP ) PORT_8WAY PORT_PLAYER(2)
	PORT_BIT( 0x22, IP_ACTIVE_HIGH, IPT_OTHER ) PORT_NAME("P2 Up Left") PORT_PLAYER(2)
	PORT_BIT( 0x44, IP_ACTIVE_HIGH, IPT_JOYSTICK_LEFT ) PORT_8WAY PORT_PLAYER(2)
	PORT_BIT( 0x88, IP_ACTIVE_HIGH, IPT_BUTTON1 ) PORT_PLAYER(2)

	PORT_START("JOY2P_1")
	PORT_BIT( 0x11, IP_ACTIVE_HIGH, IPT_OTHER ) PORT_NAME("P2 Down Left")  PORT_PLAYER(2)
	PORT_BIT( 0x22, IP_ACTIVE_HIGH, IPT_OTHER ) PORT_NAME("P2 Up Right") PORT_PLAYER(2)
	PORT_BIT( 0x44, IP_ACTIVE_HIGH, IPT_UNUSED )
	PORT_BIT( 0x88, IP_ACTIVE_HIGH, IPT_UNUSED )

	PORT_START("JOY2P_2")
	PORT_BIT( 0x11, IP_ACTIVE_HIGH, IPT_JOYSTICK_DOWN ) PORT_8WAY  PORT_PLAYER(2)
	PORT_BIT( 0x22, IP_ACTIVE_HIGH, IPT_OTHER ) PORT_NAME("P2 Down Right") PORT_PLAYER(2)
	PORT_BIT( 0x44, IP_ACTIVE_HIGH, IPT_JOYSTICK_RIGHT ) PORT_8WAY  PORT_PLAYER(2)
	PORT_BIT( 0x88, IP_ACTIVE_HIGH, IPT_BUTTON2 ) PORT_PLAYER(2)

	PORT_START("UNUSED")
	PORT_BIT( 0xff, IP_ACTIVE_HIGH, IPT_UNUSED )
INPUT_PORTS_END


static MACHINE_RESET(rx78)
{
	rx78_state *state = machine.driver_data<rx78_state>();
	state->m_cassette = machine.device("cassette");
}

static DEVICE_IMAGE_LOAD( rx78_cart )
{
	UINT8 *cart = image.device().machine().region("cart_img")->base();
	UINT32 size;

	if (image.software_entry() == NULL)
		size = image.length();
	else
		size = image.get_software_region_length("rom");

	if (size != 0x2000 && size != 0x4000)
	{
		image.seterror(IMAGE_ERROR_UNSPECIFIED, "Unsupported cartridge size");
		return IMAGE_INIT_FAIL;
	}

	if (image.software_entry() == NULL)
	{
		if (image.fread( cart, size) != size)
		{
			image.seterror(IMAGE_ERROR_UNSPECIFIED, "Unable to fully read from file");
			return IMAGE_INIT_FAIL;
		}
	}
	else
		memcpy(cart, image.get_software_region("rom"), size);

	return IMAGE_INIT_PASS;
}

/* F4 Character Displayer */
static const gfx_layout rx78_charlayout =
{
	8, 8,					/* 8 x 8 characters */
	187,					/* 187 characters */
	1,					/* 1 bits per pixel */
	{ 0 },					/* no bitplanes */
	/* x offsets */
	{ 7, 6, 5, 4, 3, 2, 1, 0 },
	/* y offsets */
	{ 0*8, 1*8, 2*8, 3*8, 4*8, 5*8, 6*8, 7*8 },
	8*8					/* every char takes 8 bytes */
};

static GFXDECODE_START( rx78 )
	GFXDECODE_ENTRY( "maincpu", 0x1a27, rx78_charlayout, 0, 8 )
GFXDECODE_END

static MACHINE_CONFIG_START( rx78, rx78_state )
	/* basic machine hardware */
	MCFG_CPU_ADD("maincpu",Z80, MASTER_CLOCK/7)	// unknown divider
	MCFG_CPU_PROGRAM_MAP(rx78_mem)
	MCFG_CPU_IO_MAP(rx78_io)
	MCFG_CPU_VBLANK_INT("screen",irq0_line_hold)

	MCFG_MACHINE_RESET(rx78)

	/* video hardware */
	MCFG_SCREEN_ADD("screen", RASTER)
	MCFG_SCREEN_REFRESH_RATE(60)
	MCFG_SCREEN_VBLANK_TIME(ATTOSECONDS_IN_USEC(2500)) /* not accurate */
	MCFG_SCREEN_FORMAT(BITMAP_FORMAT_INDEXED16)
	MCFG_SCREEN_SIZE(192, 184)
	MCFG_SCREEN_VISIBLE_AREA(0, 192-1, 0, 184-1)
	MCFG_SCREEN_UPDATE(rx78)

	MCFG_PALETTE_LENGTH(16+1) //+1 for the background color
	MCFG_GFXDECODE(rx78)

	MCFG_VIDEO_START(rx78)

	MCFG_CARTSLOT_ADD("cart")
	MCFG_CARTSLOT_EXTENSION_LIST("rom")
	MCFG_CARTSLOT_NOT_MANDATORY
	MCFG_CARTSLOT_LOAD(rx78_cart)
	MCFG_CARTSLOT_INTERFACE("rx78_cart")

	MCFG_RAM_ADD(RAM_TAG)
	MCFG_RAM_DEFAULT_SIZE("32k")
	MCFG_RAM_EXTRA_OPTIONS("16k")

	MCFG_CASSETTE_ADD( "cassette", default_cassette_config )

	MCFG_SPEAKER_STANDARD_MONO("mono")

	MCFG_SOUND_WAVE_ADD("wave", "cassette")
	MCFG_SOUND_ROUTE(ALL_OUTPUTS, "mono", 0.20)

	MCFG_SOUND_ADD("sn1", SN76489A, XTAL_28_63636MHz/8) // unknown divider
	MCFG_SOUND_ROUTE(ALL_OUTPUTS, "mono", 0.50)

	/* Software lists */
	MCFG_SOFTWARE_LIST_ADD("cart_list","rx78")
MACHINE_CONFIG_END

/* ROM definition */
ROM_START( rx78 )
    ROM_REGION( 0x10000, "maincpu", ROMREGION_ERASEFF )
	ROM_LOAD( "ipl.rom", 0x0000, 0x2000, CRC(a194ea53) SHA1(ba39e73e6eb7cbb8906fff1f81a98964cd62af0d))

	ROM_REGION( 0x4000, "cart_img", ROMREGION_ERASEFF )
	ROM_CART_LOAD("cart", 0x0000, 0x4000, ROM_OPTIONAL | ROM_NOMIRROR)

	ROM_REGION( 6 * 0x2000, "vram", ROMREGION_ERASE00 )
ROM_END

static DRIVER_INIT( rx78 )
{
	UINT32 ram_size = ram_get_size(machine.device(RAM_TAG));
	address_space *prg = machine.device("maincpu")->memory().space(AS_PROGRAM);

	if(ram_size == 0x4000)
		prg->unmap_readwrite(0x6000, 0xafff);
}

/* Driver */

/*    YEAR  NAME    PARENT  COMPAT   MACHINE    INPUT    INIT     COMPANY   FULLNAME       FLAGS */
COMP( 1983, rx78,	0,       0, 		rx78,	rx78,	 rx78,  	  "Bandai",   "Gundam RX-78",		0)

