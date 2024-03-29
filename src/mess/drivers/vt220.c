/***************************************************************************

        DEC VT220

        30/06/2009 Skeleton driver.

****************************************************************************/

#include "emu.h"
#include "cpu/mcs51/mcs51.h"
#include "machine/ram.h"


class vt220_state : public driver_device
{
public:
	vt220_state(running_machine &machine, const driver_device_config_base &config)
		: driver_device(machine, config) { }

};


static ADDRESS_MAP_START(vt220_mem, AS_PROGRAM, 8)
	AM_RANGE(0x0000, 0x7fff) AM_ROM
ADDRESS_MAP_END

static ADDRESS_MAP_START( vt220_io , AS_IO, 8)
ADDRESS_MAP_END

/* Input ports */
static INPUT_PORTS_START( vt220 )
INPUT_PORTS_END

static MACHINE_RESET(vt220)
{
	memset(ram_get_ptr(machine.device(RAM_TAG)),0,16*1024);
}

static VIDEO_START( vt220 )
{
}

static SCREEN_UPDATE( vt220 )
{
    return 0;
}


static MACHINE_CONFIG_START( vt220, vt220_state )
    /* basic machine hardware */
    MCFG_CPU_ADD("maincpu", I8051, XTAL_16MHz)
    MCFG_CPU_PROGRAM_MAP(vt220_mem)
    MCFG_CPU_IO_MAP(vt220_io)

    MCFG_MACHINE_RESET(vt220)

    /* video hardware */
    MCFG_SCREEN_ADD("screen", RASTER)
    MCFG_SCREEN_REFRESH_RATE(50)
    MCFG_SCREEN_VBLANK_TIME(ATTOSECONDS_IN_USEC(2500)) /* not accurate */
    MCFG_SCREEN_FORMAT(BITMAP_FORMAT_INDEXED16)
    MCFG_SCREEN_SIZE(640, 480)
    MCFG_SCREEN_VISIBLE_AREA(0, 640-1, 0, 480-1)
    MCFG_SCREEN_UPDATE(vt220)

    MCFG_PALETTE_LENGTH(2)
    MCFG_PALETTE_INIT(black_and_white)

    MCFG_VIDEO_START(vt220)

	/* internal ram */
	MCFG_RAM_ADD(RAM_TAG)
	MCFG_RAM_DEFAULT_SIZE("16K")
MACHINE_CONFIG_END

/* ROM definition */
ROM_START( vt220 )
	ROM_REGION( 0x10000, "maincpu", ROMREGION_ERASEFF )
	ROM_LOAD( "23-178e6.bin", 0x0000, 0x8000, CRC(cce5088c) SHA1(4638304729d1213658a96bb22c5211322b74d8fc))
ROM_END

/* Driver */

/*    YEAR  NAME    PARENT  COMPAT   MACHINE    INPUT    INIT    COMPANY   FULLNAME       FLAGS */
COMP( 1983, vt220,  0,       0, 	vt220,	vt220,	 0, 		 "Digital Equipment Corporation",   "VT220",		GAME_NOT_WORKING | GAME_NO_SOUND)

