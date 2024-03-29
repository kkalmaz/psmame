/***************************************************************************

    Konami Target Panic (cabinet test PCB)

    driver by Phil Bennett

    TODO: Determine correct clock frequencies, fix 'stuck' inputs in
    test mode

***************************************************************************/

#include "emu.h"
#include "cpu/z80/z80.h"


class tgtpanic_state : public driver_device
{
public:
	tgtpanic_state(running_machine &machine, const driver_device_config_base &config)
		: driver_device(machine, config) { }

	UINT8 *m_ram;
	UINT8 m_color;
};


/*************************************
 *
 *  Video hardware
 *
 *************************************/

static SCREEN_UPDATE( tgtpanic )
{
	tgtpanic_state *state = screen->machine().driver_data<tgtpanic_state>();
	UINT32 colors[4];
	UINT32 offs;
	UINT32 x, y;

	colors[0] = 0;
	colors[1] = 0xffffffff;
	colors[2] = MAKE_RGB(pal1bit(state->m_color >> 2), pal1bit(state->m_color >> 1), pal1bit(state->m_color >> 0));
	colors[3] = MAKE_RGB(pal1bit(state->m_color >> 6), pal1bit(state->m_color >> 5), pal1bit(state->m_color >> 4));

	for (offs = 0; offs < 0x2000; ++offs)
	{
		UINT8 val = state->m_ram[offs];

		y = (offs & 0x7f) << 1;
		x = (offs >> 7) << 2;

		/* I'm guessing the hardware doubles lines */
		*BITMAP_ADDR32(bitmap, y + 0, x + 0) = colors[val & 3];
		*BITMAP_ADDR32(bitmap, y + 1, x + 0) = colors[val & 3];
		val >>= 2;
		*BITMAP_ADDR32(bitmap, y + 0, x + 1) = colors[val & 3];
		*BITMAP_ADDR32(bitmap, y + 1, x + 1) = colors[val & 3];
		val >>= 2;
		*BITMAP_ADDR32(bitmap, y + 0, x + 2) = colors[val & 3];
		*BITMAP_ADDR32(bitmap, y + 1, x + 2) = colors[val & 3];
		val >>= 2;
		*BITMAP_ADDR32(bitmap, y + 0, x + 3) = colors[val & 3];
		*BITMAP_ADDR32(bitmap, y + 1, x + 3) = colors[val & 3];
	}

	return 0;
}

static WRITE8_HANDLER( color_w )
{
	tgtpanic_state *state = space->machine().driver_data<tgtpanic_state>();
	space->machine().primary_screen->update_partial(space->machine().primary_screen->vpos());
	state->m_color = data;
}


/*************************************
 *
 *  Address maps
 *
 *************************************/

static ADDRESS_MAP_START( prg_map, AS_PROGRAM, 8 )
	AM_RANGE(0x0000, 0x7fff) AM_ROM
	AM_RANGE(0x8000, 0xbfff) AM_RAM AM_BASE_MEMBER(tgtpanic_state, m_ram)
ADDRESS_MAP_END

static ADDRESS_MAP_START( io_map, AS_IO, 8 )
	ADDRESS_MAP_GLOBAL_MASK(0xff)
	AM_RANGE(0x00, 0x00) AM_READ_PORT("IN0") AM_WRITE(color_w)
	AM_RANGE(0x01, 0x01) AM_READ_PORT("IN1")
ADDRESS_MAP_END


/*************************************
 *
 *  Inputs
 *
 *************************************/

static INPUT_PORTS_START( tgtpanic )
	PORT_START("IN0")
	PORT_BIT( 0x01, IP_ACTIVE_LOW, IPT_JOYSTICK_UP )
	PORT_BIT( 0x02, IP_ACTIVE_LOW, IPT_JOYSTICK_DOWN )
	PORT_BIT( 0x04, IP_ACTIVE_LOW, IPT_JOYSTICK_LEFT )
	PORT_BIT( 0x08, IP_ACTIVE_LOW, IPT_JOYSTICK_RIGHT )
	PORT_BIT( 0x10, IP_ACTIVE_LOW, IPT_UNUSED )
	PORT_BIT( 0x20, IP_ACTIVE_LOW, IPT_UNUSED )
	PORT_BIT( 0x40, IP_ACTIVE_LOW, IPT_UNUSED )
	PORT_BIT( 0x80, IP_ACTIVE_LOW, IPT_UNUSED )

	PORT_START("IN1")
	PORT_BIT( 0x01, IP_ACTIVE_LOW, IPT_BUTTON1 )
	PORT_BIT( 0x02, IP_ACTIVE_LOW, IPT_SERVICE )
	PORT_BIT( 0x04, IP_ACTIVE_LOW, IPT_START1 )
	PORT_BIT( 0x08, IP_ACTIVE_LOW, IPT_COIN1 )
	PORT_BIT( 0x10, IP_ACTIVE_LOW, IPT_UNUSED )
	PORT_BIT( 0x20, IP_ACTIVE_LOW, IPT_UNUSED )
	PORT_BIT( 0x40, IP_ACTIVE_LOW, IPT_UNUSED )
	PORT_BIT( 0x80, IP_ACTIVE_LOW, IPT_UNUSED )
INPUT_PORTS_END


/*************************************
 *
 *  Machine driver
 *
 *************************************/

static MACHINE_CONFIG_START( tgtpanic, tgtpanic_state )

	/* basic machine hardware */
	MCFG_CPU_ADD("maincpu", Z80, XTAL_4MHz)
	MCFG_CPU_PROGRAM_MAP(prg_map)
	MCFG_CPU_IO_MAP(io_map)
	MCFG_CPU_PERIODIC_INT(irq0_line_hold, 20) /* Unverified */

	/* video hardware */
	MCFG_SCREEN_ADD("screen", RASTER)
	MCFG_SCREEN_REFRESH_RATE(60) /* Unverified */
	MCFG_SCREEN_VBLANK_TIME(ATTOSECONDS_IN_USEC(2500)) /* Unverified */
	MCFG_SCREEN_FORMAT(BITMAP_FORMAT_RGB32)
	MCFG_SCREEN_SIZE(256, 256)
	MCFG_SCREEN_VISIBLE_AREA(0, 192 - 1, 0, 192 - 1)
	MCFG_SCREEN_UPDATE(tgtpanic)
MACHINE_CONFIG_END


 /*************************************
 *
 *  ROM definition
 *
 *************************************/

ROM_START( tgtpanic )
	ROM_REGION( 0x10000, "maincpu", 0 )
	ROM_LOAD( "601_ja_a01.13e", 0x0000, 0x8000, CRC(ece71952) SHA1(0f9cbd8adac2b1950bc608d51f0f122399c8f00f) )
ROM_END


/*************************************
 *
 *  Game driver
 *
 *************************************/

GAME( 1996, tgtpanic, 0, tgtpanic, tgtpanic, 0, ROT0, "Konami", "Target Panic", GAME_NO_SOUND_HW )
