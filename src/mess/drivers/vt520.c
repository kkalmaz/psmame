/***************************************************************************

        DEC VT520

        02/07/2009 Skeleton driver.

****************************************************************************/

#include "emu.h"
#include "machine/ram.h"
#include "cpu/mcs51/mcs51.h"


class vt520_state : public driver_device
{
public:
	vt520_state(running_machine &machine, const driver_device_config_base &config)
		: driver_device(machine, config) { }

};


static ADDRESS_MAP_START(vt520_mem, AS_PROGRAM, 8)
	AM_RANGE(0x0000, 0xffff) AM_RAMBANK("bank1")
ADDRESS_MAP_END

/*
    On the boardthere is TC160G41AF (1222) custom chip
    doing probably all video/uart logic
    there is 43.430MHz xtal near by
*/

static READ8_HANDLER(vt520_some_r)
{
	//bit 5 0
	//bit 6 1
	return 0x40;
}

static ADDRESS_MAP_START( vt520_io , AS_IO, 8)
	ADDRESS_MAP_UNMAP_HIGH
	AM_RANGE(0x7ffb, 0x7ffb) AM_READ(vt520_some_r)
ADDRESS_MAP_END

/* Input ports */
static INPUT_PORTS_START( vt520 )
INPUT_PORTS_END


static MACHINE_RESET(vt520)
{
	address_space *space = machine.device("maincpu")->memory().space(AS_PROGRAM);
	UINT8 *rom = machine.region("maincpu")->base();
	space->unmap_write(0x0000, 0xffff);
	memory_set_bankptr(machine, "bank1", rom + 0x70000);
}

static VIDEO_START( vt520 )
{
}

static SCREEN_UPDATE( vt520 )
{
    return 0;
}

static MACHINE_CONFIG_START( vt520, vt520_state )
    /* basic machine hardware */
    MCFG_CPU_ADD("maincpu",I8032, XTAL_20MHz)
    MCFG_CPU_PROGRAM_MAP(vt520_mem)
    MCFG_CPU_IO_MAP(vt520_io)

    MCFG_MACHINE_RESET(vt520)

    /* video hardware */
    MCFG_SCREEN_ADD("screen", RASTER)
    MCFG_SCREEN_REFRESH_RATE(50)
    MCFG_SCREEN_VBLANK_TIME(ATTOSECONDS_IN_USEC(2500)) /* not accurate */
    MCFG_SCREEN_FORMAT(BITMAP_FORMAT_INDEXED16)
    MCFG_SCREEN_SIZE(802, 480)
    MCFG_SCREEN_VISIBLE_AREA(0, 802-1, 0, 480-1)
    MCFG_SCREEN_UPDATE(vt520)

    MCFG_PALETTE_LENGTH(2)
    MCFG_PALETTE_INIT(black_and_white)

    MCFG_VIDEO_START(vt520)

	// On the board there are two M5M44256BJ-7 chips
	// Which are DRAM 256K x 4bit
	/* internal ram */
	MCFG_RAM_ADD(RAM_TAG)
	MCFG_RAM_DEFAULT_SIZE("256K")
MACHINE_CONFIG_END

/* ROM definition */
ROM_START( vt520 )
    ROM_REGION( 0x80000, "maincpu", ROMREGION_ERASEFF )
	ROM_LOAD( "23-010ed-00.e20", 0x0000, 0x80000, CRC(2502cc22) SHA1(0437c3107412f69e09d050fef003f2a81d8a3163)) // "(C)DEC94 23-010ED-00 // 9739 D" dumped from a VT520-A4 model
ROM_END

/* Driver */

/*    YEAR  NAME    PARENT  COMPAT   MACHINE   INPUT    INIT     COMPANY   FULLNAME       FLAGS */
//COMP( 1993, vt510,  0,       0,   vt520,  vt520,   0,          "Digital Equipment Corporation",   "VT510",      GAME_NOT_WORKING)
COMP( 1994, vt520,  0,       0, 	vt520,	vt520,	 0, 		 "Digital Equipment Corporation",   "VT520",		GAME_NOT_WORKING | GAME_NO_SOUND)
//COMP( 1994, vt525,  0,       0,   vt520,  vt520,   0,          "Digital Equipment Corporation",   "VT525",      GAME_NOT_WORKING)
