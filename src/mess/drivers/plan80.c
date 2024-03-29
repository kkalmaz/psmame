/***************************************************************************

        Plan-80

        06/12/2009 Skeleton driver.

        Summary of Monitor commands:

        D - dump memory
        F - fill memory
        G - go (execute program at address)
        I - in from a port and display
        M - move?
        O - out to a port
        S - edit memory

        ToDo:
        - fix autorepeat on the keyboard
        - Add missing devices
        - Picture of unit shows graphics, possibly a PCG

****************************************************************************/
#define ADDRESS_MAP_MODERN


#include "emu.h"
#include "cpu/i8085/i8085.h"


class plan80_state : public driver_device
{
public:
	plan80_state(running_machine &machine, const driver_device_config_base &config)
		: driver_device(machine, config),
		m_maincpu(*this, "maincpu")
	{ }

	required_device<cpu_device> m_maincpu;
	DECLARE_READ8_MEMBER( plan80_04_r );
	DECLARE_WRITE8_MEMBER( plan80_09_w );
	UINT8* m_videoram;
	UINT8 m_kbd_row;
};

READ8_MEMBER(plan80_state::plan80_04_r)
{
	UINT8 data = 0xff;

	if (m_kbd_row == 0xfe)
		data = input_port_read(m_machine, "LINE0");
	else
	if (m_kbd_row == 0xfd)
		data = input_port_read(m_machine, "LINE1");
	else
	if (m_kbd_row == 0xfb)
		data = input_port_read(m_machine, "LINE2");
	else
	if (m_kbd_row == 0xf7)
		data = input_port_read(m_machine, "LINE3");
	else
	if (m_kbd_row == 0xef)
		data = input_port_read(m_machine, "LINE4");

	return data;
}

WRITE8_MEMBER(plan80_state::plan80_09_w)
{
	m_kbd_row = data;
}


static ADDRESS_MAP_START(plan80_mem, AS_PROGRAM, 8, plan80_state)
	ADDRESS_MAP_UNMAP_HIGH
	AM_RANGE(0x0000, 0x07ff) AM_RAMBANK("boot")
	AM_RANGE(0x0800, 0xefff) AM_RAM
	AM_RANGE(0xf000, 0xf7ff) AM_RAM AM_BASE(m_videoram)
	AM_RANGE(0xf800, 0xffff) AM_ROM
ADDRESS_MAP_END

static ADDRESS_MAP_START( plan80_io , AS_IO, 8, plan80_state)
	ADDRESS_MAP_UNMAP_HIGH
	ADDRESS_MAP_GLOBAL_MASK(0xff)
	AM_RANGE(0x04, 0x04) AM_READ(plan80_04_r)
	AM_RANGE(0x09, 0x09) AM_WRITE(plan80_09_w)
ADDRESS_MAP_END

/* Input ports */
static INPUT_PORTS_START( plan80 ) // Keyboard was worked out by trial & error;'F' keys produce foreign symbols
	PORT_START("LINE0")	/* FE */
	PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("??") PORT_CODE(KEYCODE_F1)
	PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("??") PORT_CODE(KEYCODE_F2)
	PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("Shift") PORT_CODE(KEYCODE_LSHIFT)
	PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("A -") PORT_CODE(KEYCODE_A) PORT_CHAR('A') PORT_CHAR('-')
	PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("Q ! 1") PORT_CODE(KEYCODE_Q) PORT_CHAR('Q') PORT_CHAR('!') PORT_CHAR('1')
	PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("F-shift") PORT_CODE(KEYCODE_RSHIFT)
	PORT_BIT(0x40, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("Enter") PORT_CODE(KEYCODE_ENTER) PORT_CHAR(13)
	PORT_BIT(0x80, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("P ?? 0") PORT_CODE(KEYCODE_P) PORT_CHAR('P') PORT_CHAR('0')
	PORT_START("LINE1")	/* FD */
	PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("Backspace") PORT_CODE(KEYCODE_BACKSPACE) PORT_CHAR(8)
	PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("Numbers") PORT_CODE(KEYCODE_LCONTROL)
	PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("X /") PORT_CODE(KEYCODE_X) PORT_CHAR('X') PORT_CHAR('/')
	PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("D =") PORT_CODE(KEYCODE_D) PORT_CHAR('D') PORT_CHAR('=')
	PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("E # 3") PORT_CODE(KEYCODE_E) PORT_CHAR('E') PORT_CHAR('#') PORT_CHAR('3')
	PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("M .") PORT_CODE(KEYCODE_M) PORT_CHAR('M') PORT_CHAR('.')
	PORT_BIT(0x40, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("K [") PORT_CODE(KEYCODE_K) PORT_CHAR('K') PORT_CHAR('[')
	PORT_BIT(0x80, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("I ( 8") PORT_CODE(KEYCODE_I) PORT_CHAR('I') PORT_CHAR('(') PORT_CHAR('8')
	PORT_START("LINE2")	/* FB */
	PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_UNUSED)
	PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_UNUSED)
	PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("V ;") PORT_CODE(KEYCODE_V) PORT_CHAR('V') PORT_CHAR(';')
	PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("G _") PORT_CODE(KEYCODE_G) PORT_CHAR('G') PORT_CHAR('_')
	PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("T % 5") PORT_CODE(KEYCODE_T) PORT_CHAR('T') PORT_CHAR('%') PORT_CHAR('5')
	PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("B ?") PORT_CODE(KEYCODE_B) PORT_CHAR('B') PORT_CHAR('?')
	PORT_BIT(0x40, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("H <") PORT_CODE(KEYCODE_H) PORT_CHAR('H') PORT_CHAR('<')
	PORT_BIT(0x80, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("Y & 6") PORT_CODE(KEYCODE_Y) PORT_CHAR('Y') PORT_CHAR('&') PORT_CHAR('6')
	PORT_START("LINE3")	/* F7 */
	PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("Space") PORT_CODE(KEYCODE_SPACE) PORT_CHAR(' ')
	PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_UNUSED)
	PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("C :") PORT_CODE(KEYCODE_C) PORT_CHAR('C') PORT_CHAR(':')
	PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("F ^") PORT_CODE(KEYCODE_F) PORT_CHAR('F') PORT_CHAR('^')
	PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("R $ 4") PORT_CODE(KEYCODE_R) PORT_CHAR('R') PORT_CHAR('$') PORT_CHAR('4')
	PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("N ,") PORT_CODE(KEYCODE_N) PORT_CHAR('N') PORT_CHAR(',')
	PORT_BIT(0x40, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("J >") PORT_CODE(KEYCODE_J) PORT_CHAR('J') PORT_CHAR('>')
	PORT_BIT(0x80, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("U \' 7") PORT_CODE(KEYCODE_U) PORT_CHAR('U') PORT_CHAR('\'') PORT_CHAR('7')
	PORT_START("LINE4")	/* EF */
	PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("??") PORT_CODE(KEYCODE_F3)
	PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("??") PORT_CODE(KEYCODE_F4)
	PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("Z *") PORT_CODE(KEYCODE_Z) PORT_CHAR('Z') PORT_CHAR('*')
	PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("S +") PORT_CODE(KEYCODE_S) PORT_CHAR('S') PORT_CHAR('+')
	PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("W \" 2") PORT_CODE(KEYCODE_W) PORT_CHAR('W') PORT_CHAR('\"') PORT_CHAR('2')
	PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("??") PORT_CODE(KEYCODE_F5)
	PORT_BIT(0x40, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("L ]") PORT_CODE(KEYCODE_L) PORT_CHAR('L') PORT_CHAR(']')
	PORT_BIT(0x80, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("O ) 9") PORT_CODE(KEYCODE_O) PORT_CHAR('O') PORT_CHAR(')') PORT_CHAR('9')
INPUT_PORTS_END


/* after the first 4 bytes have been read from ROM, switch the ram back in */
static TIMER_CALLBACK( plan80_boot )
{
	memory_set_bank(machine, "boot", 0);
}

static MACHINE_RESET(plan80)
{
	memory_set_bank(machine, "boot", 1);
	machine.scheduler().timer_set(attotime::from_usec(10), FUNC(plan80_boot));
}

DRIVER_INIT( plan80 )
{
	UINT8 *RAM = machine.region("maincpu")->base();
	memory_configure_bank(machine, "boot", 0, 2, &RAM[0x0000], 0xf800);
}

static VIDEO_START( plan80 )
{
}

static SCREEN_UPDATE( plan80 )
{
	plan80_state *state = screen->machine().driver_data<plan80_state>();
	UINT8 *gfx = screen->machine().region("gfx")->base();
	int x,y,j,b;
	UINT16 addr;

	for(y = 0; y < 32; y++ )
	{
		addr = y*64;
		for(x = 0; x < 48; x++ )
		{
			UINT8 code = state->m_videoram[addr + x];
			for(j = 0; j < 8; j++ )
			{
				UINT8 val = gfx[code*8 + j];
				if (BIT(code,7))  val ^= 0xff;
				for(b = 0; b < 6; b++ )
				{
					*BITMAP_ADDR16(bitmap, y*8+j, x*6+b ) = (val >> (5-b)) & 1;
				}
			}
		}
	}
	return 0;
}

/* F4 Character Displayer */
static const gfx_layout plan80_charlayout =
{
	8, 8,					/* 8 x 8 characters */
	256,					/* 256 characters */
	1,					/* 1 bits per pixel */
	{ 0 },					/* no bitplanes */
	/* x offsets */
	{ 0, 1, 2, 3, 4, 5, 6, 7 },
	/* y offsets */
	{ 0*8, 1*8, 2*8, 3*8, 4*8, 5*8, 6*8, 7*8 },
	8*8					/* every char takes 8 bytes */
};

static GFXDECODE_START( plan80 )
	GFXDECODE_ENTRY( "gfx", 0x0000, plan80_charlayout, 0, 1 )
GFXDECODE_END


static MACHINE_CONFIG_START( plan80, plan80_state )
	/* basic machine hardware */
	MCFG_CPU_ADD("maincpu",I8080, 2048000)
	MCFG_CPU_PROGRAM_MAP(plan80_mem)
	MCFG_CPU_IO_MAP(plan80_io)

	MCFG_MACHINE_RESET(plan80)

	/* video hardware */
	MCFG_SCREEN_ADD("screen", RASTER)
	MCFG_SCREEN_REFRESH_RATE(50)
	MCFG_SCREEN_VBLANK_TIME(ATTOSECONDS_IN_USEC(2500)) /* not accurate */
	MCFG_SCREEN_FORMAT(BITMAP_FORMAT_INDEXED16)
	MCFG_SCREEN_SIZE(48*6, 32*8)
	MCFG_SCREEN_VISIBLE_AREA(0, 48*6-1, 0, 32*8-1)
	MCFG_SCREEN_UPDATE(plan80)

	MCFG_GFXDECODE(plan80)
	MCFG_PALETTE_LENGTH(2)
	MCFG_PALETTE_INIT(black_and_white)

	MCFG_VIDEO_START(plan80)
MACHINE_CONFIG_END

/* ROM definition */
ROM_START( plan80 )
	ROM_REGION( 0x10000, "maincpu", 0 )
	ROM_LOAD( "pl80mon.bin", 0xf800, 0x0800, CRC(433fb685) SHA1(43d53c35544d3a197ab71b6089328d104535cfa5))

	ROM_REGION( 0x10000, "spare", 0 )
	ROM_LOAD_OPTIONAL( "pl80mod.bin", 0xf000, 0x0800, CRC(6bdd7136) SHA1(721eab193c33c9330e0817616d3d2b601285fe50))

	ROM_REGION( 0x0800, "gfx", 0 )
	ROM_LOAD( "pl80gzn.bin", 0x0000, 0x0800, CRC(b4ddbdb6) SHA1(31bf9cf0f2ed53f48dda29ea830f74cea7b9b9b2))
ROM_END

/* Driver */

/*    YEAR  NAME    PARENT  COMPAT   MACHINE    INPUT    INIT     COMPANY          FULLNAME       FLAGS */
COMP( 1988, plan80,  0,       0,     plan80,    plan80, plan80,   "Tesla Eltos",   "Plan-80", GAME_NOT_WORKING | GAME_NO_SOUND)

