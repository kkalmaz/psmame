/*

Visual 1050

PCB Layout
----------

REV B-1

|---------------------------------------------------------------------------------------------------|
|                                                               |----------|                        |
|           9216                                                |   ROM0   |        LS74            |
|                                                               |----------|                        |
--                      LS00    7406            LS255   LS393                       4164    4164    |
||      |-----------|                                           |----------|                        |
||      |   WD1793  |   LS14    7407    -       LS393   LS74    |  8251A   |        4164    4164    |
||      |-----------|                   |                       |----------|                        |
|C                                      |                                           4164    4164    |
|N      7406    LS00    LS74    LS14    |C      LS20    LS08    LS75                                |
|1                                      |N                                          4164    4164    |
||                                      |5                                                          |
||      7406    LS195           LS244   |       LS04    LS32    LS08                4164    4164    |
||                                      -                                                           |
||      |------------|  |--------|                                                  4164    4164    |
-- BAT  |   8255A    |  |  8214  |              LS139   LS32    LS138   LS17                        |
--      |------------|  |--------|      -                                           4164    4164    |
||                                      |C                                                          |
||      |--------|      LS00    LS00    |N      LS00    LS32    LS138               4164    4164    |
|C      | 8251A  |                      |6                                                          |
|N      |--------|      |------------|  -                       |------------|                      |
|2                      |   8255A    |                          |    Z80A    |                      |
||      1488 1489  RTC  |------------|          LS00    LS32    |------------|      LS257   LS257   |
||                                                                                                  |
--                                                              |------------|      |------------|  |
|                                                       LS04    |   8255A    |      |   8255A    |  |
|                                                               |------------|      |------------|  |
|                                                                                                   |
|                                               7404                                LS257   LS257   |
|                                                   16MHz                                           |
|       LS04    LS74    LS257                                                       9016    9016    |
|                                                                                                   |
--      LS74    LS04    LS74    LS163                                               9016    9016    |
||                                                                                                  |
||                                                                                  9016    9016    |
||      LS02    LS163   LS74    7404                            7404                                |
||                                                                                  9016    9016    |
||                                                                                                  |
|C      LS244   LS10    LS257                   LS362           |----------|        9016    9016    |
|N                                                              |   ROM1   |                        |
|3      LS245   7404    LS273                   LS32            |----------|        9016    9016    |
||                                                                                                  |
||      7407            LS174                   LS32                                9016    9016    |
||              15.36MHz                                                                            |
||                                                              |------------|      9016    9016    |
--                      LS09    LS04            LS12            |    6502    |                      |
|                                                               |------------|      9016    9016    |
--                                                                                                  |
|C              LS175   LS86    LS164           LS164                               9016    9016    |
|N                                                              |------------|                      |
|4                                                              |    6845    |      LS253   LS253   |
--      7426    LS02    LS74    LS00            LS164           |------------|                      |
|                                                                                   LS253   LS253   |
|                       REV B-1 W/O 1059            S/N 492                                         |
|---------------------------------------------------------------------------------------------------|

Notes:
    All IC's shown.

    ROM0    - "IC 244-032 V1.0"
    ROM1    - "IC 244-033 V1.0"
    Z80A    - Zilog Z8400APS Z80A CPU
    6502    - Synertek SY6502A CPU
    4164    - NEC D4164-2 64Kx1 Dynamic RAM
    9016    - AMD AM9016EPC 16Kx1 Dynamic RAM
    8214    - NEC uPB8214C Priority Interrupt Control Unit
    8255A   - NEC D8255AC-5 Programmable Peripheral Interface
    8251A   - NEC D8251AC Programmable Communication Interface
    WD1793  - Mitsubishi MB8877 Floppy Disc Controller
    9216    - SMC FDC9216 Floppy Disk Data Separator
    1488    - Motorola MC1488 Quad Line EIA-232D Driver
    1489    - Motorola MC1489 Quad Line Receivers
    6845    - Hitachi HD46505SP CRT Controller
    RTC     - OKI MSM58321RS Real Time Clock
    BAT     - 3.4V battery
    CN1     - parallel connector
    CN2     - serial connector
    CN3     - winchester connector
    CN4     - monitor connector
    CN5     - floppy data connector
    CN6     - floppy power connector

*/

/*

    TODO:

    - write to banked RAM at 0x0000-0x1fff when ROM is active
    - real keyboard w/i8049
    - keyboard beeper (NE555 wired in strange mix of astable/monostable modes)
    - Winchester (Tandon TM501/CMI CM-5412 10MB drive on Xebec S1410 controller)

*/

#define ADDRESS_MAP_MODERN

#include "emu.h"
#include "includes/v1050.h"
#include "cpu/z80/z80.h"
#include "cpu/m6502/m6502.h"
#include "cpu/mcs48/mcs48.h"
#include "imagedev/flopdrv.h"
#include "machine/ram.h"
#include "formats/basicdsk.h"
#include "machine/ctronics.h"
#include "machine/i8214.h"
#include "machine/i8255a.h"
#include "machine/msm58321.h"
#include "machine/msm8251.h"
#include "machine/wd17xx.h"
#include "video/mc6845.h"
#include "sound/discrete.h"

void v1050_state::set_interrupt(UINT8 mask, int state)
{
	if (state)
	{
		m_int_state |= mask;
	}
	else
	{
		m_int_state &= ~mask;
	}

	i8214_r_w(m_pic, 0, ~(m_int_state & m_int_mask));
}

void v1050_state::bankswitch()
{
	address_space *program = m_maincpu->memory().space(AS_PROGRAM);

	int bank = (m_bank >> 1) & 0x03;

	if (BIT(m_bank, 0))
	{
		program->install_readwrite_bank(0x0000, 0x1fff, "bank1");
		memory_set_bank(m_machine, "bank1", bank);
	}
	else
	{
		program->install_read_bank(0x0000, 0x1fff, "bank1");
		program->unmap_write(0x0000, 0x1fff);
		memory_set_bank(m_machine, "bank1", 3);
	}

	memory_set_bank(m_machine, "bank2", bank);

	if (bank == 2)
	{
		program->unmap_readwrite(0x4000, 0xbfff);
	}
	else
	{
		program->install_readwrite_bank(0x4000, 0x7fff, "bank3");
		program->install_readwrite_bank(0x8000, 0xbfff, "bank4");
		memory_set_bank(m_machine, "bank3", bank);
		memory_set_bank(m_machine, "bank4", bank);
	}

	memory_set_bank(m_machine, "bank5", bank);
}

/* Keyboard HACK */

static const UINT8 V1050_KEYCODES[4][12][8] =
{
	{   /* unshifted */
		{ 0xc0, 0xd4, 0xd8, 0xdc, 0xe0, 0xe4, 0xe8, 0xec },
		{ 0xf0, 0xfc, 0x90, 0xf4, 0xf8, 0x94, 0xc4, 0xc8 },
		{ 0xcc, 0xd0, 0x1b, 0x31, 0x32, 0x33, 0x34, 0x35 },
		{ 0x36, 0x37, 0x38, 0x39, 0x30, 0x2d, 0x3d, 0x60 },
		{ 0x08, 0x88, 0x8c, 0x71, 0x77, 0x65, 0x72, 0x74 },
		{ 0x79, 0x75, 0x69, 0x6f, 0x70, 0x5b, 0x5d, 0x0d },
		{ 0x7f, 0x00, 0x80, 0x61, 0x73, 0x64, 0x66, 0x67 },
		{ 0x68, 0x6a, 0x6b, 0x6c, 0x3b, 0x27, 0x5c, 0x84 },
		{ 0x00, 0x7a, 0x78, 0x63, 0x76, 0x62, 0x6e, 0x6d },
		{ 0x2c, 0x2e, 0x2f, 0x00, 0x0a, 0x20, 0x81, 0x82 },
		{ 0xb7, 0xb8, 0xb9, 0xad, 0xb4, 0xb5, 0xb6, 0xac },
		{ 0xb1, 0xb2, 0xb3, 0x83, 0xb0, 0xae, 0x00, 0x00 },
	},

	{	/* shifted */
		{ 0xc1, 0xd5, 0xd9, 0xdd, 0xe1, 0xe5, 0xe9, 0xed },
		{ 0xf1, 0xfd, 0x91, 0xf5, 0xf9, 0x95, 0xc5, 0xc9 },
		{ 0xcd, 0xd1, 0x1b, 0x21, 0x40, 0x23, 0x24, 0x25 },
		{ 0x5e, 0x26, 0x2a, 0x28, 0x29, 0x5f, 0x2b, 0x7e },
		{ 0x08, 0x89, 0x8d, 0x51, 0x57, 0x45, 0x52, 0x54 },
		{ 0x59, 0x55, 0x49, 0x4f, 0x50, 0x7b, 0x7d, 0x0d },
		{ 0x7f, 0x00, 0x80, 0x41, 0x53, 0x44, 0x46, 0x47 },
		{ 0x48, 0x4a, 0x4b, 0x4c, 0x3a, 0x22, 0x7c, 0x85 },
		{ 0x00, 0x5a, 0x58, 0x43, 0x56, 0x42, 0x4e, 0x4d },
		{ 0x3c, 0x3e, 0x3f, 0x0a, 0x20, 0x81, 0x82, 0xb7 },
		{ 0xb8, 0xb9, 0xad, 0xb4, 0xb5, 0xb6, 0xac, 0xb1 },
		{ 0xb2, 0xb3, 0x83, 0xb0, 0xa0, 0x00, 0x00, 0x00 },
	},

	{	/* control */
		{ 0xc2, 0xd6, 0xda, 0xde, 0xe2, 0xe6, 0xea, 0xee },
		{ 0xf2, 0xfe, 0x92, 0xf6, 0xfa, 0x96, 0xc6, 0xca },
		{ 0xce, 0xd2, 0x1b, 0x31, 0x32, 0x33, 0x34, 0x35 },
		{ 0x36, 0x37, 0x38, 0x39, 0x30, 0x2d, 0x3d, 0x00 },
		{ 0x08, 0x8a, 0x8e, 0x11, 0x17, 0x05, 0x12, 0x14 },
		{ 0x19, 0x15, 0x09, 0x0f, 0x10, 0x1b, 0x1d, 0x0d },
		{ 0x7f, 0x00, 0x80, 0x01, 0x13, 0x04, 0x06, 0x07 },
		{ 0x08, 0x0a, 0x0b, 0x0c, 0x3b, 0x27, 0x1c, 0x86 },
		{ 0x00, 0x1a, 0x18, 0x03, 0x16, 0x02, 0x0e, 0x0d },
		{ 0x2c, 0x2e, 0x2f, 0x0a, 0x20, 0x81, 0x82, 0xb7 },
		{ 0xb8, 0xb9, 0xad, 0xb4, 0xb5, 0xb6, 0xac, 0xb1 },
		{ 0xb2, 0xb3, 0x83, 0xb0, 0xae, 0x00, 0x00, 0x00 },
	},

	{	/* shift & control */
		{ 0xc3, 0xd7, 0xdb, 0xdf, 0xe3, 0xe7, 0xeb, 0xef },
		{ 0xf3, 0xff, 0x93, 0xf7, 0xfb, 0x97, 0xc7, 0xcb },
		{ 0xcf, 0xd3, 0x1b, 0x21, 0x00, 0x23, 0x24, 0x25 },
		{ 0x1e, 0x26, 0x2a, 0x28, 0x29, 0x1f, 0x2b, 0x1e },
		{ 0x08, 0x8b, 0x8f, 0x11, 0x17, 0x05, 0x12, 0x14 },
		{ 0x19, 0x15, 0x09, 0x0f, 0x10, 0x1b, 0x1d, 0x0d },
		{ 0x7f, 0x00, 0x80, 0x01, 0x13, 0x04, 0x06, 0x07 },
		{ 0x08, 0x0a, 0x0b, 0x0c, 0x3a, 0x22, 0x1c, 0x87 },
		{ 0x00, 0x1a, 0x18, 0x03, 0x16, 0x02, 0x0e, 0x0d },
		{ 0x3c, 0x3e, 0x3f, 0x0a, 0x20, 0x81, 0x82, 0xb7 },
		{ 0xb8, 0xb9, 0xad, 0xb4, 0xb5, 0xb6, 0xac, 0xb1 },
		{ 0xb2, 0xb3, 0x83, 0xb0, 0xae, 0x00, 0x00, 0x00 },
	}
};

void v1050_state::scan_keyboard()
{
	static const char *const keynames[] = { "ROW0", "ROW1", "ROW2", "ROW3", "ROW4", "ROW5", "ROW6", "ROW7", "ROW8", "ROW9", "ROW10", "ROW11" };
	int table = 0, row, col;
	int keydata = 0xff;

	UINT8 line_mod = input_port_read(m_machine, "ROW12");

	if((line_mod & 0x07) && (line_mod & 0x18))
	{
		table = 3;  /* shift & control */
	}
	else if (line_mod & 0x07)
	{
		table = 1; /* shifted */
	}
	else if (line_mod & 0x18)
	{
		table = 2; /* ctrl */
	}

	/* scan keyboard */
	for (row = 0; row < 12; row++)
	{
		UINT8 data = input_port_read(m_machine, keynames[row]);

		for (col = 0; col < 8; col++)
		{
			if (!BIT(data, col))
			{
				/* latch key data */
				keydata = V1050_KEYCODES[table][row][col];

				if (m_keydata != keydata)
				{
					m_keydata = keydata;
					m_keyavail = 1;

					set_interrupt(INT_KEYBOARD, 1);
					return;
				}
			}
		}
	}

	m_keydata = keydata;
}

static TIMER_DEVICE_CALLBACK( v1050_keyboard_tick )
{
	v1050_state *state = timer.machine().driver_data<v1050_state>();

	state->scan_keyboard();
}

READ8_MEMBER( v1050_state::kb_data_r )
{
	m_keyavail = 0;

	set_interrupt(INT_KEYBOARD, 0);

	return m_keydata;
}

READ8_MEMBER( v1050_state::kb_status_r )
{
	UINT8 val =	msm8251_status_r(m_uart_kb, 0);

	return val | (m_keyavail ? 0x02 : 0x00);
}

/* Z80 Read/Write Handlers */

WRITE8_MEMBER( v1050_state::v1050_i8214_w )
{
	i8214_b_w(m_pic, 0, (data >> 1) & 0x0f);
}

READ8_MEMBER( v1050_state::vint_clr_r )
{
	set_interrupt(INT_VSYNC, 0);

	return 0xff;
}

WRITE8_MEMBER( v1050_state::vint_clr_w )
{
	set_interrupt(INT_VSYNC, 0);
}

READ8_MEMBER( v1050_state::dint_clr_r )
{
	set_interrupt(INT_DISPLAY, 0);

	return 0xff;
}

WRITE8_MEMBER( v1050_state::dint_clr_w )
{
	set_interrupt(INT_DISPLAY, 0);
}

WRITE8_MEMBER( v1050_state::bank_w )
{
	m_bank = data;

	bankswitch();
}

/* SY6502A Read/Write Handlers */

WRITE8_MEMBER( v1050_state::dint_w )
{
	set_interrupt(INT_DISPLAY, 1);
}

WRITE8_MEMBER( v1050_state::dvint_clr_w )
{
	device_set_input_line(m_subcpu, INPUT_LINE_IRQ0, CLEAR_LINE);
}

/* i8049 Read/Write Handlers */

READ8_MEMBER( v1050_state::keyboard_r )
{
    static const char *const KEY_ROW[] = { "X0", "X1", "X2", "X3", "X4", "X5", "X6", "X7", "X8", "X9", "XA", "XB" };

    return input_port_read(m_machine, KEY_ROW[m_keylatch]);
}

WRITE8_MEMBER( v1050_state::keyboard_w )
{
    m_keylatch = data & 0x0f;
}

WRITE8_MEMBER( v1050_state::p2_w )
{
	/*

        bit     description

        P20
        P21
        P22
        P23
        P24
        P25     led output
        P26     speaker (NE555) output
        P27     serial output

    */

	// led output
	output_set_led_value(0, BIT(data, 5));

	// speaker output
	discrete_sound_w(m_discrete, NODE_01, BIT(data, 6));

	// serial output
	m_kb_so = BIT(data, 7);
}

/* Memory Maps */

static ADDRESS_MAP_START( v1050_mem, AS_PROGRAM, 8, v1050_state )
	ADDRESS_MAP_UNMAP_HIGH
	AM_RANGE(0x0000, 0x1fff) AM_RAMBANK("bank1")
	AM_RANGE(0x2000, 0x3fff) AM_RAMBANK("bank2")
	AM_RANGE(0x4000, 0x7fff) AM_RAMBANK("bank3")
	AM_RANGE(0x8000, 0xbfff) AM_RAMBANK("bank4")
	AM_RANGE(0xc000, 0xffff) AM_RAMBANK("bank5")
ADDRESS_MAP_END

static ADDRESS_MAP_START( v1050_io, AS_IO, 8, v1050_state )
	ADDRESS_MAP_UNMAP_HIGH
	ADDRESS_MAP_GLOBAL_MASK(0xff)
	AM_RANGE(0x84, 0x87) AM_DEVREADWRITE_LEGACY(I8255A_DISP_TAG, i8255a_r, i8255a_w)
//  AM_RANGE(0x88, 0x88) AM_DEVREADWRITE_LEGACY(I8251A_KB_TAG, msm8251_data_r, msm8251_data_w)
//  AM_RANGE(0x89, 0x89) AM_DEVREADWRITE_LEGACY(I8251A_KB_TAG, msm8251_status_r, msm8251_control_w)
	AM_RANGE(0x88, 0x88) AM_READ(kb_data_r) AM_DEVWRITE_LEGACY(I8251A_KB_TAG, msm8251_data_w)
	AM_RANGE(0x89, 0x89) AM_READ(kb_status_r) AM_DEVWRITE_LEGACY(I8251A_KB_TAG, msm8251_control_w)
	AM_RANGE(0x8c, 0x8c) AM_DEVREADWRITE_LEGACY(I8251A_SIO_TAG, msm8251_data_r, msm8251_data_w)
	AM_RANGE(0x8d, 0x8d) AM_DEVREADWRITE_LEGACY(I8251A_SIO_TAG, msm8251_status_r, msm8251_control_w)
	AM_RANGE(0x90, 0x93) AM_DEVREADWRITE_LEGACY(I8255A_MISC_TAG, i8255a_r, i8255a_w)
	AM_RANGE(0x94, 0x97) AM_DEVREADWRITE_LEGACY(MB8877_TAG, wd17xx_r, wd17xx_w)
	AM_RANGE(0x9c, 0x9f) AM_DEVREADWRITE_LEGACY(I8255A_RTC_TAG, i8255a_r, i8255a_w)
	AM_RANGE(0xa0, 0xa0) AM_READWRITE(vint_clr_r, vint_clr_w)
	AM_RANGE(0xb0, 0xb0) AM_READWRITE(dint_clr_r, dint_clr_w)
	AM_RANGE(0xc0, 0xc0) AM_WRITE(v1050_i8214_w)
	AM_RANGE(0xd0, 0xd0) AM_WRITE(bank_w)
//  AM_RANGE(0xe0, 0xe3) AM_DEVREADWRITE(S1410_TAG, s1410_r, s1410_w)
ADDRESS_MAP_END

static ADDRESS_MAP_START( v1050_crt_mem, AS_PROGRAM, 8, v1050_state )
	AM_RANGE(0x0000, 0x7fff) AM_READWRITE(videoram_r, videoram_w) AM_BASE(m_video_ram)
	AM_RANGE(0x8000, 0x8000) AM_DEVWRITE_LEGACY(H46505_TAG, mc6845_address_w)
	AM_RANGE(0x8001, 0x8001) AM_DEVREADWRITE_LEGACY(H46505_TAG, mc6845_register_r, mc6845_register_w)
	AM_RANGE(0x9000, 0x9003) AM_DEVREADWRITE_LEGACY(I8255A_M6502_TAG, i8255a_r, i8255a_w)
	AM_RANGE(0xa000, 0xa000) AM_READWRITE(attr_r, attr_w)
	AM_RANGE(0xb000, 0xb000) AM_WRITE(dint_w)
	AM_RANGE(0xc000, 0xc000) AM_WRITE(dvint_clr_w)
	AM_RANGE(0xe000, 0xffff) AM_ROM
ADDRESS_MAP_END

static ADDRESS_MAP_START( v1050_kbd_io, AS_IO, 8, v1050_state )
	AM_RANGE(MCS48_PORT_P1, MCS48_PORT_P1) AM_READWRITE(keyboard_r, keyboard_w)
	AM_RANGE(MCS48_PORT_P2, MCS48_PORT_P2) AM_WRITE(p2_w)
ADDRESS_MAP_END

/* Input Ports */

static INPUT_PORTS_START( v1050_real )
	PORT_START("X0")
	PORT_BIT( 0x01, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_NAME("Left Shift") PORT_CODE(KEYCODE_LSHIFT) PORT_CHAR(UCHAR_SHIFT_1)
	PORT_BIT( 0x02, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_NAME("Ctrl") PORT_CODE(KEYCODE_LCONTROL) PORT_CHAR(UCHAR_MAMEKEY(LCONTROL))
	PORT_BIT( 0x04, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_NAME("Caps Lock") PORT_CODE(KEYCODE_CAPSLOCK) PORT_CHAR(UCHAR_MAMEKEY(CAPSLOCK))
	PORT_BIT( 0x08, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_NAME("Right Shift") PORT_CODE(KEYCODE_RSHIFT)
	PORT_BIT( 0x10, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_NAME("Help")
	PORT_BIT( 0x20, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_NAME("Esc") PORT_CODE(KEYCODE_ESC) PORT_CHAR(UCHAR_MAMEKEY(ESC))
	PORT_BIT( 0x40, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_NAME("Tab") PORT_CODE(KEYCODE_TAB) PORT_CHAR(UCHAR_MAMEKEY(TAB))
	PORT_BIT( 0x80, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_NAME("No Scrl")

	PORT_START("X1")
	PORT_BIT( 0x01, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_NAME("Keypad Enter DelCh") PORT_CODE(KEYCODE_ENTER_PAD) PORT_CHAR(UCHAR_MAMEKEY(ENTER_PAD))
	PORT_BIT( 0x02, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_NAME("Keypad 6 Pg Dn") PORT_CODE(KEYCODE_6_PAD) PORT_CHAR(UCHAR_MAMEKEY(6_PAD))
	PORT_BIT( 0x04, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_NAME("Keypad 3 \xE2\x86\x92") PORT_CODE(KEYCODE_3_PAD) PORT_CHAR(UCHAR_MAMEKEY(3_PAD))
	PORT_BIT( 0x08, IP_ACTIVE_LOW, IPT_UNUSED )
	PORT_BIT( 0x10, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_NAME("Keypad - DelLn") PORT_CODE(KEYCODE_MINUS_PAD) PORT_CHAR(UCHAR_MAMEKEY(MINUS_PAD))
	PORT_BIT( 0x20, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_NAME("Keypad 9 End") PORT_CODE(KEYCODE_9_PAD) PORT_CHAR(UCHAR_MAMEKEY(9_PAD))
	PORT_BIT( 0x40, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_NAME("Keypad , DelWd")
	PORT_BIT( 0x80, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_NAME("Keypad . Wd\xE2\x86\x92") PORT_CODE(KEYCODE_DEL_PAD) PORT_CHAR(UCHAR_MAMEKEY(DEL_PAD))

	PORT_START("X2")
	PORT_BIT( 0x01, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_NAME("F14 OnSer")
	PORT_BIT( 0x02, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_NAME("F16 Print")
	PORT_BIT( 0x04, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_NAME("F12 Copy") PORT_CODE(KEYCODE_F12) PORT_CHAR(UCHAR_MAMEKEY(F12))
	PORT_BIT( 0x08, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_NAME("F10 End") PORT_CODE(KEYCODE_F10) PORT_CHAR(UCHAR_MAMEKEY(F10))
	PORT_BIT( 0x10, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_NAME("F15 Quick")
	PORT_BIT( 0x20, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_NAME("F11 Move") PORT_CODE(KEYCODE_F11) PORT_CHAR(UCHAR_MAMEKEY(F11))
	PORT_BIT( 0x40, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_NAME("F17 Block")
	PORT_BIT( 0x80, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_NAME("F13 Hide")

	PORT_START("X3")
	PORT_BIT( 0x01, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_NAME("Line Feed")
	PORT_BIT( 0x02, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_CODE(KEYCODE_BACKSLASH) PORT_CHAR('|') PORT_CHAR('\\')
	PORT_BIT( 0x04, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_NAME("Retn") PORT_CODE(KEYCODE_ENTER) PORT_CHAR(13)
	PORT_BIT( 0x08, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_NAME("Num Lock") PORT_CODE(KEYCODE_NUMLOCK) PORT_CHAR(UCHAR_MAMEKEY(NUMLOCK))
	PORT_BIT( 0x10, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_NAME("Break Stop")
	PORT_BIT( 0x20, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_NAME("Back Space") PORT_CODE(KEYCODE_BACKSPACE) PORT_CHAR(8)
	PORT_BIT( 0x40, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_NAME("Del") PORT_CODE(KEYCODE_DEL) PORT_CHAR(UCHAR_MAMEKEY(DEL))
	PORT_BIT( 0x80, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_NAME("Keypad 0 Wd\xE2\x86\x90") PORT_CODE(KEYCODE_0_PAD) PORT_CHAR(UCHAR_MAMEKEY(0_PAD))

	PORT_START("X4")
	PORT_BIT( 0x01, IP_ACTIVE_LOW, IPT_UNUSED )
	PORT_BIT( 0x02, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_NAME("Keypad 5 \xE2\x86\x91") PORT_CODE(KEYCODE_5_PAD) PORT_CHAR(UCHAR_MAMEKEY(5_PAD))
	PORT_BIT( 0x04, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_NAME("Keypad 1 \xE2\x86\x90") PORT_CODE(KEYCODE_1_PAD) PORT_CHAR(UCHAR_MAMEKEY(1_PAD))
	PORT_BIT( 0x08, IP_ACTIVE_LOW, IPT_UNUSED )
	PORT_BIT( 0x10, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_NAME("Keypad 7 Home") PORT_CODE(KEYCODE_7_PAD) PORT_CHAR(UCHAR_MAMEKEY(7_PAD))
	PORT_BIT( 0x20, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_NAME("Keypad 8 Last") PORT_CODE(KEYCODE_8_PAD) PORT_CHAR(UCHAR_MAMEKEY(8_PAD))
	PORT_BIT( 0x40, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_NAME("Keypad 4 PgUp") PORT_CODE(KEYCODE_4_PAD) PORT_CHAR(UCHAR_MAMEKEY(4_PAD))
	PORT_BIT( 0x80, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_NAME("Keypad 2 \xE2\x86\x93") PORT_CODE(KEYCODE_2_PAD) PORT_CHAR(UCHAR_MAMEKEY(2_PAD))

	PORT_START("X5")
	PORT_BIT( 0x01, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_CODE(KEYCODE_K) PORT_CHAR('k') PORT_CHAR('K')
	PORT_BIT( 0x02, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_CODE(KEYCODE_P) PORT_CHAR('p') PORT_CHAR('P')
	PORT_BIT( 0x04, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_CODE(KEYCODE_H) PORT_CHAR('h') PORT_CHAR('H')
	PORT_BIT( 0x08, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_CODE(KEYCODE_L) PORT_CHAR('l') PORT_CHAR('L')
	PORT_BIT( 0x10, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_CODE(KEYCODE_U) PORT_CHAR('u') PORT_CHAR('U')
	PORT_BIT( 0x20, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_CODE(KEYCODE_I) PORT_CHAR('i') PORT_CHAR('I')
	PORT_BIT( 0x40, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_CODE(KEYCODE_O) PORT_CHAR('o') PORT_CHAR('O')
	PORT_BIT( 0x80, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_CODE(KEYCODE_J) PORT_CHAR('j') PORT_CHAR('J')

	PORT_START("X6")
	PORT_BIT( 0x01, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_CODE(KEYCODE_V) PORT_CHAR('v') PORT_CHAR('V')
	PORT_BIT( 0x02, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_CODE(KEYCODE_6) PORT_CHAR('6') PORT_CHAR('^')
	PORT_BIT( 0x04, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_CODE(KEYCODE_F) PORT_CHAR('f') PORT_CHAR('F')
	PORT_BIT( 0x08, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_CODE(KEYCODE_N) PORT_CHAR('n') PORT_CHAR('N')
	PORT_BIT( 0x10, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_CODE(KEYCODE_T) PORT_CHAR('t') PORT_CHAR('T')
	PORT_BIT( 0x20, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_CODE(KEYCODE_SPACE) PORT_CHAR(' ')
	PORT_BIT( 0x40, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_CODE(KEYCODE_Y) PORT_CHAR('y') PORT_CHAR('Y')
	PORT_BIT( 0x80, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_CODE(KEYCODE_8) PORT_CHAR('8') PORT_CHAR('*')

	PORT_START("X7")
	PORT_BIT( 0x01, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_NAME("F8 Insert") PORT_CODE(KEYCODE_F8) PORT_CHAR(UCHAR_MAMEKEY(F8))
	PORT_BIT( 0x02, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_NAME("F5") PORT_CODE(KEYCODE_F5) PORT_CHAR(UCHAR_MAMEKEY(F5))
	PORT_BIT( 0x04, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_NAME("F6 InsCr") PORT_CODE(KEYCODE_F6) PORT_CHAR(UCHAR_MAMEKEY(F6))
	PORT_BIT( 0x08, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_NAME("F9 Begin") PORT_CODE(KEYCODE_F9) PORT_CHAR(UCHAR_MAMEKEY(F9))
	PORT_BIT( 0x10, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_NAME("F3 Rept") PORT_CODE(KEYCODE_F3) PORT_CHAR(UCHAR_MAMEKEY(F3))
	PORT_BIT( 0x20, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_NAME("F2 Find") PORT_CODE(KEYCODE_F2) PORT_CHAR(UCHAR_MAMEKEY(F2))
	PORT_BIT( 0x40, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_NAME("F4	Again") PORT_CODE(KEYCODE_F4) PORT_CHAR(UCHAR_MAMEKEY(F4))
	PORT_BIT( 0x80, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_NAME("F7 Refs") PORT_CODE(KEYCODE_F7) PORT_CHAR(UCHAR_MAMEKEY(F7))

	PORT_START("X8")
	PORT_BIT( 0x01, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_CODE(KEYCODE_X) PORT_CHAR('x') PORT_CHAR('X')
	PORT_BIT( 0x02, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_CODE(KEYCODE_R) PORT_CHAR('r') PORT_CHAR('R')
	PORT_BIT( 0x04, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_CODE(KEYCODE_D) PORT_CHAR('d') PORT_CHAR('D')
	PORT_BIT( 0x08, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_CODE(KEYCODE_C) PORT_CHAR('c') PORT_CHAR('C')
	PORT_BIT( 0x10, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_NAME("F1 Save") PORT_CODE(KEYCODE_F1) PORT_CHAR(UCHAR_MAMEKEY(F1))
	PORT_BIT( 0x20, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_CODE(KEYCODE_1) PORT_CHAR('1') PORT_CHAR('!')
	PORT_BIT( 0x40, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_CODE(KEYCODE_E) PORT_CHAR('e') PORT_CHAR('E')
	PORT_BIT( 0x80, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_CODE(KEYCODE_S) PORT_CHAR('s') PORT_CHAR('S')

	PORT_START("X9")
	PORT_BIT( 0x01, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_CODE(KEYCODE_A) PORT_CHAR('a') PORT_CHAR('A')
	PORT_BIT( 0x02, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_CODE(KEYCODE_W) PORT_CHAR('w') PORT_CHAR('W')
	PORT_BIT( 0x04, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_CODE(KEYCODE_4) PORT_CHAR('4') PORT_CHAR('$')
	PORT_BIT( 0x08, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_CODE(KEYCODE_Z) PORT_CHAR('z') PORT_CHAR('Z')
	PORT_BIT( 0x10, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_CODE(KEYCODE_2) PORT_CHAR('2') PORT_CHAR('@')
	PORT_BIT( 0x20, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_CODE(KEYCODE_3) PORT_CHAR('3') PORT_CHAR('#')
	PORT_BIT( 0x40, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_CODE(KEYCODE_Q) PORT_CHAR('q') PORT_CHAR('Q')
	PORT_BIT( 0x80, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_CODE(KEYCODE_5) PORT_CHAR('5') PORT_CHAR('%')

	PORT_START("XA")
	PORT_BIT( 0x01, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_CODE(KEYCODE_TILDE) PORT_CHAR('~') PORT_CHAR('\'')
	PORT_BIT( 0x02, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_CODE(KEYCODE_9) PORT_CHAR('0') PORT_CHAR(')')
	PORT_BIT( 0x04, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_CODE(KEYCODE_0) PORT_CHAR('9') PORT_CHAR('(')
	PORT_BIT( 0x08, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_CODE(KEYCODE_EQUALS) PORT_CHAR('=') PORT_CHAR('+')
	PORT_BIT( 0x10, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_CODE(KEYCODE_6) PORT_CHAR('6') PORT_CHAR('^')
	PORT_BIT( 0x20, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_CODE(KEYCODE_MINUS) PORT_CHAR('-') PORT_CHAR('_')
	PORT_BIT( 0x40, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_CODE(KEYCODE_8) PORT_CHAR('8') PORT_CHAR('*')
	PORT_BIT( 0x80, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_CODE(KEYCODE_7) PORT_CHAR('7') PORT_CHAR('&')

	PORT_START("XB")
	PORT_BIT( 0x01, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_CODE(KEYCODE_COMMA) PORT_CHAR(',') PORT_CHAR('<')
	PORT_BIT( 0x02, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_CODE(KEYCODE_COLON) PORT_CHAR(';') PORT_CHAR(':')
	PORT_BIT( 0x04, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_CODE(KEYCODE_QUOTE) PORT_CHAR('\'') PORT_CHAR('"')
	PORT_BIT( 0x08, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_CODE(KEYCODE_STOP) PORT_CHAR('.') PORT_CHAR('>')
	PORT_BIT( 0x10, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_CODE(KEYCODE_OPENBRACE) PORT_CHAR('[') PORT_CHAR('{')
	PORT_BIT( 0x20, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_CODE(KEYCODE_SLASH) PORT_CHAR('/') PORT_CHAR('?')
	PORT_BIT( 0x40, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_CODE(KEYCODE_CLOSEBRACE) PORT_CHAR(']') PORT_CHAR('}')
	PORT_BIT( 0x80, IP_ACTIVE_LOW, IPT_KEYBOARD )
INPUT_PORTS_END

static INPUT_PORTS_START( v1050 )
	PORT_START("ROW0")
	PORT_BIT( 0x01, IP_ACTIVE_LOW, IPT_KEYBOARD ) /* HELP */
	PORT_BIT( 0x02, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_CODE(KEYCODE_F1)
	PORT_BIT( 0x04, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_CODE(KEYCODE_F2)
	PORT_BIT( 0x08, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_CODE(KEYCODE_F3)
	PORT_BIT( 0x10, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_CODE(KEYCODE_F4)
	PORT_BIT( 0x20, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_CODE(KEYCODE_F5)
	PORT_BIT( 0x40, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_CODE(KEYCODE_F6)
	PORT_BIT( 0x80, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_CODE(KEYCODE_F7)

	PORT_START("ROW1")
	PORT_BIT( 0x01, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_CODE(KEYCODE_F8)
	PORT_BIT( 0x02, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_CODE(KEYCODE_F9)
	PORT_BIT( 0x04, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_CODE(KEYCODE_F10)
	PORT_BIT( 0x08, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_CODE(KEYCODE_F11)
	PORT_BIT( 0x10, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_CODE(KEYCODE_F12)
	PORT_BIT( 0x20, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_CODE(KEYCODE_F13)
	PORT_BIT( 0x40, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_CODE(KEYCODE_F14)
	PORT_BIT( 0x80, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_CODE(KEYCODE_F15)

	PORT_START("ROW2")
	PORT_BIT( 0x01, IP_ACTIVE_LOW, IPT_KEYBOARD ) /* F16 */
	PORT_BIT( 0x02, IP_ACTIVE_LOW, IPT_KEYBOARD ) /* F17 */
	PORT_BIT( 0x04, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_NAME("ESC") PORT_CODE(KEYCODE_ESC) PORT_CHAR(UCHAR_MAMEKEY(ESC))
	PORT_BIT( 0x08, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_CODE(KEYCODE_1) PORT_CHAR('1') PORT_CHAR('!')
	PORT_BIT( 0x10, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_CODE(KEYCODE_2) PORT_CHAR('2') PORT_CHAR('@')
	PORT_BIT( 0x20, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_CODE(KEYCODE_3) PORT_CHAR('3') PORT_CHAR('#')
	PORT_BIT( 0x40, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_CODE(KEYCODE_4) PORT_CHAR('4') PORT_CHAR('$')
	PORT_BIT( 0x80, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_CODE(KEYCODE_5) PORT_CHAR('5') PORT_CHAR('%')

	PORT_START("ROW3")
	PORT_BIT( 0x01, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_CODE(KEYCODE_6) PORT_CHAR('6') PORT_CHAR('^')
	PORT_BIT( 0x02, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_CODE(KEYCODE_7) PORT_CHAR('7') PORT_CHAR('&')
	PORT_BIT( 0x04, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_CODE(KEYCODE_8) PORT_CHAR('8') PORT_CHAR('*')
	PORT_BIT( 0x08, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_CODE(KEYCODE_9) PORT_CHAR('9') PORT_CHAR('(')
	PORT_BIT( 0x10, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_CODE(KEYCODE_0) PORT_CHAR('0') PORT_CHAR(')')
	PORT_BIT( 0x20, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_CODE(KEYCODE_MINUS) PORT_CHAR('-') PORT_CHAR('_')
	PORT_BIT( 0x40, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_CODE(KEYCODE_EQUALS) PORT_CHAR('=') PORT_CHAR('+')
	PORT_BIT( 0x80, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_CODE(KEYCODE_TILDE) PORT_CHAR('`') PORT_CHAR('~')

	PORT_START("ROW4")
	PORT_BIT( 0x01, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_NAME("BACKSPACE") PORT_CODE(KEYCODE_BACKSPACE) PORT_CHAR(8)
	PORT_BIT( 0x02, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_NAME("BREAK") PORT_CODE(KEYCODE_PAUSE)
	PORT_BIT( 0x04, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_NAME("TAB") PORT_CODE(KEYCODE_TAB) PORT_CHAR('\t')
	PORT_BIT( 0x08, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_CODE(KEYCODE_Q) PORT_CHAR('q') PORT_CHAR('Q')
	PORT_BIT( 0x10, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_CODE(KEYCODE_W) PORT_CHAR('w') PORT_CHAR('W')
	PORT_BIT( 0x20, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_CODE(KEYCODE_E) PORT_CHAR('e') PORT_CHAR('E')
	PORT_BIT( 0x40, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_CODE(KEYCODE_R) PORT_CHAR('r') PORT_CHAR('R')
	PORT_BIT( 0x80, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_CODE(KEYCODE_T) PORT_CHAR('t') PORT_CHAR('T')

	PORT_START("ROW5")
	PORT_BIT( 0x01, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_CODE(KEYCODE_Y) PORT_CHAR('y') PORT_CHAR('Y')
	PORT_BIT( 0x02, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_CODE(KEYCODE_U) PORT_CHAR('u') PORT_CHAR('U')
	PORT_BIT( 0x04, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_CODE(KEYCODE_I) PORT_CHAR('i') PORT_CHAR('I')
	PORT_BIT( 0x08, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_CODE(KEYCODE_O) PORT_CHAR('o') PORT_CHAR('O')
	PORT_BIT( 0x10, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_CODE(KEYCODE_P) PORT_CHAR('p') PORT_CHAR('P')
	PORT_BIT( 0x20, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_CODE(KEYCODE_OPENBRACE) PORT_CHAR('[') PORT_CHAR('{')
	PORT_BIT( 0x40, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_CODE(KEYCODE_CLOSEBRACE) PORT_CHAR(']') PORT_CHAR('}')
	PORT_BIT( 0x80, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_NAME("RETURN") PORT_CODE(KEYCODE_ENTER) PORT_CHAR('\r')

	PORT_START("ROW6")
	PORT_BIT( 0x01, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_NAME("DEL") PORT_CODE(KEYCODE_DEL)
	PORT_BIT( 0x02, IP_ACTIVE_LOW, IPT_KEYBOARD )
	PORT_BIT( 0x04, IP_ACTIVE_LOW, IPT_KEYBOARD )
	PORT_BIT( 0x08, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_CODE(KEYCODE_A) PORT_CHAR('a') PORT_CHAR('A')
	PORT_BIT( 0x10, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_CODE(KEYCODE_S) PORT_CHAR('s') PORT_CHAR('S')
	PORT_BIT( 0x20, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_CODE(KEYCODE_D) PORT_CHAR('d') PORT_CHAR('D')
	PORT_BIT( 0x40, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_CODE(KEYCODE_F) PORT_CHAR('f') PORT_CHAR('F')
	PORT_BIT( 0x80, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_CODE(KEYCODE_G) PORT_CHAR('g') PORT_CHAR('G')

	PORT_START("ROW7")
	PORT_BIT( 0x01, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_CODE(KEYCODE_H) PORT_CHAR('h') PORT_CHAR('H')
	PORT_BIT( 0x02, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_CODE(KEYCODE_J) PORT_CHAR('j') PORT_CHAR('J')
	PORT_BIT( 0x04, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_CODE(KEYCODE_K) PORT_CHAR('k') PORT_CHAR('K')
	PORT_BIT( 0x08, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_CODE(KEYCODE_L) PORT_CHAR('l') PORT_CHAR('L')
	PORT_BIT( 0x10, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_CODE(KEYCODE_COLON) PORT_CHAR(';') PORT_CHAR(':')
	PORT_BIT( 0x20, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_CODE(KEYCODE_QUOTE) PORT_CHAR('\'') PORT_CHAR('"')
	PORT_BIT( 0x40, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_CODE(KEYCODE_BACKSLASH) PORT_CHAR('\\') PORT_CHAR('|')
	PORT_BIT( 0x80, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_CODE(KEYCODE_SCRLOCK)

	PORT_START("ROW8")
	PORT_BIT( 0x01, IP_ACTIVE_LOW, IPT_KEYBOARD ) /* SHIFT */
	PORT_BIT( 0x02, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_CODE(KEYCODE_Z) PORT_CHAR('z') PORT_CHAR('Z')
	PORT_BIT( 0x04, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_CODE(KEYCODE_X) PORT_CHAR('x') PORT_CHAR('X')
	PORT_BIT( 0x08, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_CODE(KEYCODE_C) PORT_CHAR('c') PORT_CHAR('C')
	PORT_BIT( 0x10, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_CODE(KEYCODE_V) PORT_CHAR('v') PORT_CHAR('V')
	PORT_BIT( 0x20, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_CODE(KEYCODE_B) PORT_CHAR('b') PORT_CHAR('B')
	PORT_BIT( 0x40, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_CODE(KEYCODE_N) PORT_CHAR('n') PORT_CHAR('N')
	PORT_BIT( 0x80, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_CODE(KEYCODE_M) PORT_CHAR('m') PORT_CHAR('M')

	PORT_START("ROW9")
	PORT_BIT( 0x01, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_CODE(KEYCODE_COMMA) PORT_CHAR(',') PORT_CHAR('<')
	PORT_BIT( 0x02, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_CODE(KEYCODE_STOP) PORT_CHAR('.') PORT_CHAR('>')
	PORT_BIT( 0x04, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_CODE(KEYCODE_SLASH) PORT_CHAR('/') PORT_CHAR('?')
	PORT_BIT( 0x08, IP_ACTIVE_LOW, IPT_KEYBOARD ) /* SHIFT */
	PORT_BIT( 0x10, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_NAME("LINE FEED") PORT_CODE(KEYCODE_ENTER_PAD)
	PORT_BIT( 0x20, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_CODE(KEYCODE_SPACE)
	PORT_BIT( 0x40, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_CODE(KEYCODE_NUMLOCK) /* DOWN */
	PORT_BIT( 0x80, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_CODE(KEYCODE_NUMLOCK) /* UP */

	PORT_START("ROW10")
	PORT_BIT( 0x01, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_NAME("Keypad 7") PORT_CODE(KEYCODE_7_PAD)
	PORT_BIT( 0x02, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_NAME("Keypad 8") PORT_CODE(KEYCODE_8_PAD)
	PORT_BIT( 0x04, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_NAME("Keypad 9") PORT_CODE(KEYCODE_9_PAD)
	PORT_BIT( 0x08, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_NAME("Keypad -") PORT_CODE(KEYCODE_MINUS_PAD)
	PORT_BIT( 0x10, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_NAME("Keypad 4") PORT_CODE(KEYCODE_4_PAD)
	PORT_BIT( 0x20, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_NAME("Keypad 5") PORT_CODE(KEYCODE_5_PAD)
	PORT_BIT( 0x40, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_NAME("Keypad 6") PORT_CODE(KEYCODE_6_PAD)
	PORT_BIT( 0x80, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_NAME(UTF8_UP) PORT_CODE(KEYCODE_UP) PORT_CHAR(UCHAR_MAMEKEY(UP))

	PORT_START("ROW11")
	PORT_BIT( 0x01, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_NAME("Keypad 1") PORT_CODE(KEYCODE_1_PAD)
	PORT_BIT( 0x02, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_NAME("Keypad 2") PORT_CODE(KEYCODE_2_PAD)
	PORT_BIT( 0x04, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_NAME("Keypad 3") PORT_CODE(KEYCODE_3_PAD)
	PORT_BIT( 0x08, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_NAME("Keypad 0") PORT_CODE(KEYCODE_0_PAD)
	PORT_BIT( 0x10, IP_ACTIVE_LOW, IPT_KEYBOARD ) PORT_NAME("Keypad +") PORT_CODE(KEYCODE_PLUS_PAD)
	PORT_BIT( 0x20, IP_ACTIVE_LOW, IPT_KEYBOARD )
	PORT_BIT( 0x40, IP_ACTIVE_LOW, IPT_KEYBOARD )
	PORT_BIT( 0x80, IP_ACTIVE_LOW, IPT_KEYBOARD )

	PORT_START("ROW12")
	PORT_BIT( 0x01, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_NAME("LOCK") PORT_CODE(KEYCODE_CAPSLOCK) PORT_TOGGLE
	PORT_BIT( 0x02, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_NAME("LEFT SHIFT") PORT_CODE(KEYCODE_LSHIFT) PORT_CHAR(UCHAR_SHIFT_1)
	PORT_BIT( 0x04, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_NAME("RIGHT SHIFT") PORT_CODE(KEYCODE_RSHIFT)
	PORT_BIT( 0x08, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_NAME("LEFT CTRL") PORT_CODE(KEYCODE_LCONTROL) PORT_CHAR(UCHAR_MAMEKEY(LCONTROL))
	PORT_BIT( 0x10, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_NAME("RIGHT CTRL") PORT_CODE(KEYCODE_RCONTROL) PORT_CHAR(UCHAR_MAMEKEY(RCONTROL))
INPUT_PORTS_END

/* Sound */

static const discrete_555_desc v1050_ne555 =
{
	DISC_555_OUT_SQW | DISC_555_OUT_DC,
	5,		// B+ voltage of 555
	DEFAULT_555_VALUES
};

static DISCRETE_SOUND_START( v1050 )
	DISCRETE_INPUT_LOGIC(NODE_01)
	DISCRETE_555_ASTABLE(NODE_02, NODE_01, RES_K(68) /* can't read on schematic */ , RES_K(3), CAP_N(10), &v1050_ne555)
	DISCRETE_OUTPUT(NODE_02, 5000)
DISCRETE_SOUND_END

/* 8214 Interface */

static WRITE_LINE_DEVICE_HANDLER( pic_int_w )
{
	if (state == ASSERT_LINE)
	{
		device_set_input_line(device, INPUT_LINE_IRQ0, ASSERT_LINE);
	}
}

static I8214_INTERFACE( pic_intf )
{
	DEVCB_DEVICE_LINE(Z80_TAG, pic_int_w),
	DEVCB_NULL
};

/* MSM58321 Interface */

static MSM58321_INTERFACE( rtc_intf )
{
	DEVCB_NULL
};

/* Display 8255A Interface */

static WRITE8_DEVICE_HANDLER( disp_ppi_pc_w )
{
	i8255a_pc2_w(device, BIT(data, 6));
	i8255a_pc4_w(device, BIT(data, 7));
}

static I8255A_INTERFACE( disp_ppi_intf )
{
	DEVCB_DEVICE_HANDLER(I8255A_M6502_TAG, i8255a_pb_r),	// Port A read
	DEVCB_NULL,							// Port B read
	DEVCB_NULL,							// Port C read
	DEVCB_NULL,							// Port A write
	DEVCB_NULL,							// Port B write
	DEVCB_DEVICE_HANDLER(I8255A_M6502_TAG, disp_ppi_pc_w)		// Port C write
};

static WRITE8_DEVICE_HANDLER( m6502_ppi_pc_w )
{
	i8255a_pc2_w(device, BIT(data, 7));
	i8255a_pc4_w(device, BIT(data, 6));
}

static I8255A_INTERFACE( m6502_ppi_intf )
{
	DEVCB_DEVICE_HANDLER(I8255A_DISP_TAG, i8255a_pb_r),	// Port A read
	DEVCB_NULL,							// Port B read
	DEVCB_NULL,							// Port C read
	DEVCB_NULL,							// Port A write
	DEVCB_NULL,							// Port B write
	DEVCB_DEVICE_HANDLER(I8255A_DISP_TAG, m6502_ppi_pc_w)	// Port C write
};

/* Miscellanous 8255A Interface */

WRITE8_MEMBER( v1050_state::misc_ppi_pa_w )
{
	/*

        bit     signal      description

        PA0     f_ds<0>     drive 0 select
        PA1     f_ds<1>     drive 1 select
        PA2     f_ds<2>     drive 2 select
        PA3     f_ds<3>     drive 3 select
        PA4     f_side_1    floppy side select
        PA5     f_pre_comp  precompensation
        PA6     f_motor_on* floppy motor
        PA7     f_dden*     double density select

    */

	int f_motor_on = !BIT(data, 6);

	/* floppy drive select */
	if (!BIT(data, 0)) wd17xx_set_drive(m_fdc, 0);
	if (!BIT(data, 1)) wd17xx_set_drive(m_fdc, 1);
	if (!BIT(data, 2)) wd17xx_set_drive(m_fdc, 2);
	if (!BIT(data, 3)) wd17xx_set_drive(m_fdc, 3);

	/* floppy side select */
	wd17xx_set_side(m_fdc, BIT(data, 4));

	/* floppy motor */
	floppy_mon_w(m_floppy0, BIT(data, 6));
	floppy_mon_w(m_floppy1, BIT(data, 6));
	floppy_drive_set_ready_state(m_floppy0, f_motor_on, 1);
	floppy_drive_set_ready_state(m_floppy1, f_motor_on, 1);

	/* density select */
	wd17xx_dden_w(m_fdc, BIT(data, 7));
}

static WRITE8_DEVICE_HANDLER( misc_ppi_pb_w )
{
	centronics_data_w(device, 0, ~data & 0xff);
}

static READ8_DEVICE_HANDLER( misc_ppi_pc_r )
{
	/*

        bit     signal      description

        PC0     pr_strobe   printer strobe
        PC1     f_int_enb   floppy interrupt enable
        PC2     baud_sel_a
        PC3     baud_sel_b
        PC4     pr_busy*    printer busy
        PC5     pr_pe*      printer paper end
        PC6
        PC7

    */

	UINT8 data = 0;

	data |= centronics_not_busy_r(device) << 4;
	data |= centronics_pe_r(device) << 5;

	return data;
}

WRITE8_MEMBER( v1050_state::misc_ppi_pc_w )
{
	/*

        bit     signal      description

        PC0     pr_strobe   printer strobe
        PC1     f_int_enb   floppy interrupt enable
        PC2     baud_sel_a
        PC3     baud_sel_b
        PC4     pr_busy*    printer busy
        PC5     pr_pe*      printer paper end
        PC6
        PC7

    */

	/* printer strobe */
	centronics_strobe_w(m_centronics, BIT(data, 0));

	/* floppy interrupt enable */
	m_f_int_enb = BIT(data, 1);

	if (!m_f_int_enb)
	{
		set_interrupt(INT_FLOPPY, 0);
		device_set_input_line(m_maincpu, INPUT_LINE_NMI, CLEAR_LINE);
	}

	/* baud select */
	int baud_sel = (data >> 2) & 0x03;

	if (baud_sel != m_baud_sel)
	{
		attotime period = attotime::never;

		switch (baud_sel)
		{
		case 0:	period = attotime::from_hz((double)XTAL_16MHz/4/13/16); break;
		case 1:	period = attotime::from_hz((double)XTAL_16MHz/4/13/8); break;
		case 2:	period = attotime::from_hz((double)XTAL_16MHz/4/8); break;
		case 3:	period = attotime::from_hz((double)XTAL_16MHz/4/13/2); break;
		}

		m_timer_sio->adjust(attotime::zero, 0, period);

		m_baud_sel = baud_sel;
	}
}

static I8255A_INTERFACE( misc_ppi_intf )
{
	DEVCB_NULL,							// Port A read
	DEVCB_NULL,							// Port B read
	DEVCB_DEVICE_HANDLER(CENTRONICS_TAG, misc_ppi_pc_r),		// Port C read
	DEVCB_DRIVER_MEMBER(v1050_state, misc_ppi_pa_w),		// Port A write
	DEVCB_DEVICE_HANDLER(CENTRONICS_TAG, misc_ppi_pb_w),		// Port B write
	DEVCB_DRIVER_MEMBER(v1050_state, misc_ppi_pc_w)		// Port C write
};

/* Real Time Clock 8255A Interface */

WRITE8_MEMBER( v1050_state::rtc_ppi_pb_w )
{
	/*

        bit     signal      description

        PB0                 RS-232
        PB1                 Winchester
        PB2                 keyboard
        PB3                 floppy disk interrupt
        PB4                 vertical interrupt
        PB5                 display interrupt
        PB6                 expansion B
        PB7                 expansion A

    */

	m_int_mask = data;
}

static READ8_DEVICE_HANDLER( rtc_ppi_pc_r )
{
	/*

        bit     signal      description

        PC0
        PC1
        PC2
        PC3                 clock busy
        PC4                 clock address write
        PC5                 clock data write
        PC6                 clock data read
        PC7                 clock device select

    */

	return msm58321_busy_r(device) << 3;
}

static WRITE8_DEVICE_HANDLER( rtc_ppi_pc_w )
{
	/*

        bit     signal      description

        PC0
        PC1
        PC2
        PC3                 clock busy
        PC4                 clock address write
        PC5                 clock data write
        PC6                 clock data read
        PC7                 clock device select

    */

	msm58321_address_write_w(device, BIT(data, 4));
	msm58321_write_w(device, BIT(data, 5));
	msm58321_read_w(device, BIT(data, 6));
	msm58321_cs2_w(device, BIT(data, 7));
}

static I8255A_INTERFACE( rtc_ppi_intf )
{
	DEVCB_DEVICE_HANDLER(MSM58321RS_TAG, msm58321_r),	// Port A read
	DEVCB_NULL,							// Port B read
	DEVCB_DEVICE_HANDLER(MSM58321RS_TAG, rtc_ppi_pc_r),		// Port C read
	DEVCB_DEVICE_HANDLER(MSM58321RS_TAG, msm58321_w),	// Port A write
	DEVCB_DRIVER_MEMBER(v1050_state, rtc_ppi_pb_w),		// Port B write
	DEVCB_DEVICE_HANDLER(MSM58321RS_TAG, rtc_ppi_pc_w)			// Port C write
};

/* Keyboard 8251A Interface */

static TIMER_DEVICE_CALLBACK( kb_8251_tick )
{
	v1050_state *state = timer.machine().driver_data<v1050_state>();

	msm8251_transmit_clock(state->m_uart_kb);
	msm8251_receive_clock(state->m_uart_kb);
}

WRITE_LINE_MEMBER( v1050_state::kb_rxrdy_w )
{
	set_interrupt(INT_KEYBOARD, state);
}

static const msm8251_interface kb_8251_intf =
{
	DEVCB_NULL,
	DEVCB_NULL,
	DEVCB_NULL,
	DEVCB_NULL,
	DEVCB_NULL,
	DEVCB_DRIVER_LINE_MEMBER(v1050_state, kb_rxrdy_w),
	DEVCB_NULL,
	DEVCB_NULL,
	DEVCB_NULL
};

/* Serial 8251A Interface */

static TIMER_DEVICE_CALLBACK( sio_8251_tick )
{
	v1050_state *state = timer.machine().driver_data<v1050_state>();

	msm8251_transmit_clock(state->m_uart_sio);
	msm8251_receive_clock(state->m_uart_sio);
}

WRITE_LINE_MEMBER( v1050_state::sio_rxrdy_w )
{
	m_rxrdy = state;

	set_interrupt(INT_RS_232, m_rxrdy | m_txrdy);
}

WRITE_LINE_MEMBER( v1050_state::sio_txrdy_w )
{
	m_txrdy = state;

	set_interrupt(INT_RS_232, m_rxrdy | m_txrdy);
}

static const msm8251_interface sio_8251_intf =
{
	DEVCB_NULL,
	DEVCB_NULL,
	DEVCB_NULL,
	DEVCB_NULL,
	DEVCB_NULL,
	DEVCB_DRIVER_LINE_MEMBER(v1050_state, sio_rxrdy_w),
	DEVCB_DRIVER_LINE_MEMBER(v1050_state, sio_txrdy_w),
	DEVCB_NULL,
	DEVCB_NULL
};

/* MB8877 Interface */

WRITE_LINE_MEMBER( v1050_state::fdc_intrq_w )
{
	if (m_f_int_enb)
	{
		set_interrupt(INT_FLOPPY, state);
	}
	else
	{
		set_interrupt(INT_FLOPPY, 0);
	}
}

WRITE_LINE_MEMBER( v1050_state::fdc_drq_w )
{
	if (m_f_int_enb)
	{
		device_set_input_line(m_maincpu, INPUT_LINE_NMI, state);
	}
	else
	{
		device_set_input_line(m_maincpu, INPUT_LINE_NMI, CLEAR_LINE);
	}
}

static const wd17xx_interface fdc_intf =
{
	DEVCB_NULL,
	DEVCB_DRIVER_LINE_MEMBER(v1050_state, fdc_intrq_w),
	DEVCB_DRIVER_LINE_MEMBER(v1050_state, fdc_drq_w),
	{ FLOPPY_0, FLOPPY_1, NULL, NULL }
};

static FLOPPY_OPTIONS_START( v1050 )
	FLOPPY_OPTION( v1050, "dsk", "Visual 1050 disk image", basicdsk_identify_default, basicdsk_construct_default,
		HEADS([1])
		TRACKS([80])
		SECTORS([10])
		SECTOR_LENGTH([512])
		FIRST_SECTOR_ID([1]))
FLOPPY_OPTIONS_END

static const floppy_config v1050_floppy_config =
{
	DEVCB_NULL,
	DEVCB_NULL,
	DEVCB_NULL,
	DEVCB_NULL,
	DEVCB_NULL,
	FLOPPY_STANDARD_5_25_DSHD,
	FLOPPY_OPTIONS_NAME(v1050),
	"floppy_5_25"
};

/* Machine Initialization */

static IRQ_CALLBACK( v1050_int_ack )
{
	v1050_state *state = device->machine().driver_data<v1050_state>();

	UINT8 vector = 0xf0 | (i8214_a_r(state->m_pic, 0) << 1);

	//logerror("Interrupt Acknowledge Vector: %02x\n", vector);

	device_set_input_line(state->m_maincpu, INPUT_LINE_IRQ0, CLEAR_LINE);

	return vector;
}

void v1050_state::machine_start()
{
	address_space *program = m_maincpu->memory().space(AS_PROGRAM);

	/* initialize I8214 */
	i8214_etlg_w(m_pic, 1);
	i8214_inte_w(m_pic, 1);

	/* initialize RTC */
	msm58321_cs1_w(m_rtc, 1);

	/* set CPU interrupt callback */
	device_set_irq_callback(m_maincpu, v1050_int_ack);

	/* setup memory banking */
	UINT8 *ram = ram_get_ptr(m_machine.device(RAM_TAG));

	memory_configure_bank(m_machine, "bank1", 0, 2, ram, 0x10000);
	memory_configure_bank(m_machine, "bank1", 2, 1, ram + 0x1c000, 0);
	memory_configure_bank(m_machine, "bank1", 3, 1, m_machine.region(Z80_TAG)->base(), 0);

	program->install_readwrite_bank(0x2000, 0x3fff, "bank2");
	memory_configure_bank(m_machine, "bank2", 0, 2, ram + 0x2000, 0x10000);
	memory_configure_bank(m_machine, "bank2", 2, 1, ram + 0x1e000, 0);

	program->install_readwrite_bank(0x4000, 0x7fff, "bank3");
	memory_configure_bank(m_machine, "bank3", 0, 2, ram + 0x4000, 0x10000);

	program->install_readwrite_bank(0x8000, 0xbfff, "bank4");
	memory_configure_bank(m_machine, "bank4", 0, 2, ram + 0x8000, 0x10000);

	program->install_readwrite_bank(0xc000, 0xffff, "bank5");
	memory_configure_bank(m_machine, "bank5", 0, 3, ram + 0xc000, 0);

	bankswitch();

	/* register for state saving */
	state_save_register_global(m_machine, m_int_mask);
	state_save_register_global(m_machine, m_int_state);
	state_save_register_global(m_machine, m_f_int_enb);
	state_save_register_global(m_machine, m_keylatch);
	state_save_register_global(m_machine, m_keydata);
	state_save_register_global(m_machine, m_keyavail);
	state_save_register_global(m_machine, m_kb_so);
	state_save_register_global(m_machine, m_rxrdy);
	state_save_register_global(m_machine, m_txrdy);
	state_save_register_global(m_machine, m_baud_sel);
	state_save_register_global(m_machine, m_bank);
}

void v1050_state::machine_reset()
{
	m_bank = 0;

	bankswitch();

	m_timer_sio->adjust(attotime::zero, 0, attotime::from_hz((double)XTAL_16MHz/4/13/16));
}

/* Machine Driver */

static MACHINE_CONFIG_START( v1050, v1050_state )
	/* basic machine hardware */
    MCFG_CPU_ADD(Z80_TAG, Z80, XTAL_16MHz/4)
    MCFG_CPU_PROGRAM_MAP(v1050_mem)
    MCFG_CPU_IO_MAP(v1050_io)
	MCFG_QUANTUM_PERFECT_CPU(Z80_TAG)

    MCFG_CPU_ADD(M6502_TAG, M6502, XTAL_15_36MHz/16)
    MCFG_CPU_PROGRAM_MAP(v1050_crt_mem)
	MCFG_QUANTUM_PERFECT_CPU(M6502_TAG)

	MCFG_CPU_ADD(I8049_TAG, I8049, XTAL_4_608MHz)
	MCFG_CPU_IO_MAP(v1050_kbd_io)
	MCFG_DEVICE_DISABLE()

	/* keyboard HACK */
	MCFG_TIMER_ADD_PERIODIC("keyboard", v1050_keyboard_tick, attotime::from_hz(60))

    /* video hardware */
	MCFG_FRAGMENT_ADD(v1050_video)

	/* sound hardware */
	MCFG_SPEAKER_STANDARD_MONO("mono")
    MCFG_SOUND_ADD(DISCRETE_TAG, DISCRETE, 0)
    MCFG_SOUND_CONFIG_DISCRETE(v1050)
    MCFG_SOUND_ROUTE(ALL_OUTPUTS, "mono", 0.25)

	/* devices */
	MCFG_I8214_ADD(UPB8214_TAG, XTAL_16MHz/4, pic_intf)
	MCFG_MSM58321RS_ADD(MSM58321RS_TAG, XTAL_32_768kHz, rtc_intf)
	MCFG_I8255A_ADD(I8255A_DISP_TAG, disp_ppi_intf)
	MCFG_I8255A_ADD(I8255A_MISC_TAG, misc_ppi_intf)
	MCFG_I8255A_ADD(I8255A_RTC_TAG, rtc_ppi_intf)
	MCFG_I8255A_ADD(I8255A_M6502_TAG, m6502_ppi_intf)
	MCFG_MSM8251_ADD(I8251A_KB_TAG, /*XTAL_16MHz/8,*/ kb_8251_intf)
	MCFG_MSM8251_ADD(I8251A_SIO_TAG, /*XTAL_16MHz/8,*/ sio_8251_intf)
	MCFG_WD1793_ADD(MB8877_TAG, /*XTAL_16MHz/16,*/ fdc_intf )
	MCFG_FLOPPY_2_DRIVES_ADD(v1050_floppy_config)
	MCFG_TIMER_ADD_PERIODIC(TIMER_KB_TAG, kb_8251_tick, attotime::from_hz((double)XTAL_16MHz/4/13/8))
	MCFG_TIMER_ADD(TIMER_SIO_TAG, sio_8251_tick)

	/* software lists */
	MCFG_SOFTWARE_LIST_ADD("disk_list","v1050")

	/* printer */
	MCFG_CENTRONICS_ADD(CENTRONICS_TAG, standard_centronics)

	/* internal ram */
	MCFG_RAM_ADD(RAM_TAG)
	MCFG_RAM_DEFAULT_SIZE("128K")
MACHINE_CONFIG_END

/* ROMs */

ROM_START( v1050 )
	ROM_REGION( 0x10000, Z80_TAG, 0 )
	ROM_LOAD( "e244-032 rev 1.2.u86", 0x0000, 0x2000, CRC(46f847a7) SHA1(374db7a38a9e9230834ce015006e2f1996b9609a) )

	ROM_REGION( 0x10000, M6502_TAG, 0 )
	ROM_LOAD( "e244-033 rev 1.1.u77", 0xe000, 0x2000, CRC(c0502b66) SHA1(bc0015f5b14f98110e652eef9f7c57c614683be5) )

	ROM_REGION( 0x800, I8049_TAG, 0 )
	ROM_LOAD( "20-08049-410.z5", 0x0000, 0x0800, NO_DUMP )
	ROM_LOAD( "22-02716-074.z6", 0x0000, 0x0800, NO_DUMP )
ROM_END

/* System Drivers */

/*    YEAR  NAME    PARENT  COMPAT  MACHINE INPUT   INIT    COMPANY                     FULLNAME        FLAGS */
COMP( 1983, v1050,	0,		0,		v1050,	v1050,	0,		"Visual Technology Inc",	"Visual 1050",	GAME_IMPERFECT_GRAPHICS | GAME_SUPPORTS_SAVE | GAME_NO_SOUND)
