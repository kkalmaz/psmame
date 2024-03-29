/******************************************************************************
 PeT mess@utanet.at September 2000
******************************************************************************/

#include "emu.h"
#include "machine/mos6530.h"
#include "cpu/m6502/m6502.h"
#include "sound/dac.h"
#include "mk2.lh"


class mk2_state : public driver_device
{
public:
	mk2_state(running_machine &machine, const driver_device_config_base &config)
		: driver_device(machine, config) { }

	UINT8 m_led[5];
};


/* usage:

   under the black keys are operations to be added as first sign
   black and white box are only changing the player

   for the computer to start as white
    switch to black (h enter)
    swap players (g enter)
*/
/*
chess champion mk II

MOS MPS 6504 2179
MOS MPS 6530 024 1879
 layout of 6530 dumped with my adapter
 0x1300-0x133f io
 0x1380-0x13bf ram
 0x1400-0x17ff rom

2x2111 ram (256x4?)
MOS MPS 6332 005 2179
74145 bcd to decimal encoder (10 low active select lines)
(7400)

4x 7 segment led display (each with dot)
4 single leds
21 keys
*/

/*
  83, 84 contains display variables
 */
// only lower 12 address bits on bus!
static ADDRESS_MAP_START(mk2_mem , AS_PROGRAM, 8)
	ADDRESS_MAP_GLOBAL_MASK(0x1FFF) // m6504
	AM_RANGE( 0x0000, 0x01ff) AM_RAM // 2 2111, should be mirrored
	AM_RANGE( 0x0b00, 0x0b0f) AM_DEVREADWRITE("miot", mos6530_r, mos6530_w)
	AM_RANGE( 0x0b80, 0x0bbf) AM_RAM // rriot ram
	AM_RANGE( 0x0c00, 0x0fff) AM_ROM // rriot rom
	AM_RANGE( 0x1000, 0x1fff) AM_ROM
ADDRESS_MAP_END

static INPUT_PORTS_START( mk2 )
	PORT_START("EXTRA")
	PORT_BIT(0x001, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("NEW GAME") PORT_CODE(KEYCODE_F3) // seems to be direct wired to reset
	PORT_BIT(0x002, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("CLEAR") PORT_CODE(KEYCODE_F1)
	PORT_BIT(0x004, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("ENTER") PORT_CODE(KEYCODE_ENTER)
	PORT_START("BLACK")
	PORT_BIT(0x001, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("Black A    Black") PORT_CODE(KEYCODE_A)
	PORT_BIT(0x002, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("Black B    Field") PORT_CODE(KEYCODE_B)
	PORT_BIT(0x004, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("Black C    Time?") PORT_CODE(KEYCODE_C)
	PORT_BIT(0x008, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("Black D    Time?") PORT_CODE(KEYCODE_D)
	PORT_BIT(0x010, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("Black E    Time off?") PORT_CODE(KEYCODE_E)
	PORT_BIT(0x020, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("Black F    LEVEL") PORT_CODE(KEYCODE_F)
	PORT_BIT(0x040, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("Black G    Swap") PORT_CODE(KEYCODE_G)
	PORT_BIT(0x080, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("Black H    White") PORT_CODE(KEYCODE_H)
	PORT_START("WHITE")
	PORT_BIT(0x001, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("White 1") PORT_CODE(KEYCODE_1)
	PORT_BIT(0x002, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("White 2") PORT_CODE(KEYCODE_2)
	PORT_BIT(0x004, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("White 3") PORT_CODE(KEYCODE_3)
	PORT_BIT(0x008, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("White 4") PORT_CODE(KEYCODE_4)
	PORT_BIT(0x010, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("White 5") PORT_CODE(KEYCODE_5)
	PORT_BIT(0x020, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("White 6") PORT_CODE(KEYCODE_6)
	PORT_BIT(0x040, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("White 7") PORT_CODE(KEYCODE_7)
	PORT_BIT(0x080, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("White 8") PORT_CODE(KEYCODE_8)
INPUT_PORTS_END


static TIMER_DEVICE_CALLBACK( update_leds )
{
	mk2_state *state = timer.machine().driver_data<mk2_state>();
	int i;

	for (i=0; i<4; i++)
	output_set_digit_value(i, state->m_led[i]);
	output_set_led_value(0, state->m_led[4]&8?1:0);
	output_set_led_value(1, state->m_led[4]&0x20?1:0);
	output_set_led_value(2, state->m_led[4]&0x10?1:0);
	output_set_led_value(3, state->m_led[4]&0x10?0:1);

	state->m_led[0]= state->m_led[1]= state->m_led[2]= state->m_led[3]= state->m_led[4]= 0;
}

static MACHINE_START( mk2 )
{
}

static READ8_DEVICE_HANDLER( mk2_read_a )
{
	int data=0xff;
	int help=input_port_read(device->machine(), "BLACK")|input_port_read(device->machine(), "WHITE"); // looks like white and black keys are the same!

	switch (mos6530_portb_out_get(device)&0x7) {
	case 4:
		if (help&0x20) data&=~0x1; //F
		if (help&0x10) data&=~0x2; //E
		if (help&8) data&=~0x4; //D
		if (help&4) data&=~0x8; // C
		if (help&2) data&=~0x10; // B
		if (help&1) data&=~0x20; // A
#if 0
		if (input_port_read(device->machine(), "???")&1) data&=~0x40; //?
#endif
		break;
	case 5:
#if 0
		if (input_port_read(device->machine(), "???")&2) data&=~0x1; //?
		if (input_port_read(device->machine(), "???")&4) data&=~0x2; //?
		if (input_port_read(device->machine(), "???")&8) data&=~0x4; //?
#endif
		if (input_port_read(device->machine(), "EXTRA")&4) data&=~0x8; // Enter
		if (input_port_read(device->machine(), "EXTRA")&2) data&=~0x10; // Clear
		if (help&0x80) data&=~0x20; // H
		if (help&0x40) data&=~0x40; // G
		break;
	}
	return data;
}

static WRITE8_DEVICE_HANDLER( mk2_write_a )
{
	mk2_state *state = device->machine().driver_data<mk2_state>();
	int temp = mos6530_portb_out_get(device);

	switch(temp&0x3) {
	case 0: case 1: case 2: case 3:
		state->m_led[temp&3]|=data;
	}
}


static READ8_DEVICE_HANDLER( mk2_read_b )
{
	return 0xff&~0x40; // chip select mapped to pb6
}


static WRITE8_DEVICE_HANDLER( mk2_write_b )
{
	mk2_state *state = device->machine().driver_data<mk2_state>();
	device_t *dac_device = device->machine().device("dac");

	if ((data&0x06)==0x06)
		dac_data_w(dac_device,data&1?80:0);
	state->m_led[4]|=data;

	cputag_set_input_line( device->machine(), "maincpu", M6502_IRQ_LINE, (data & 0x80) ? CLEAR_LINE : ASSERT_LINE );
}

static MOS6530_INTERFACE( mk2_mos6530_interface )
{
	DEVCB_HANDLER(mk2_read_a),
	DEVCB_HANDLER(mk2_write_a),
	DEVCB_HANDLER(mk2_read_b),
	DEVCB_HANDLER(mk2_write_b)
};


static MACHINE_CONFIG_START( mk2, mk2_state )
	/* basic machine hardware */
	MCFG_CPU_ADD("maincpu", M6502, 1000000)        /* 6504 */
	MCFG_CPU_PROGRAM_MAP(mk2_mem)
	MCFG_QUANTUM_TIME(attotime::from_hz(60))

	MCFG_MACHINE_START( mk2 )

    /* video hardware */
	MCFG_DEFAULT_LAYOUT(layout_mk2)

	MCFG_MOS6530_ADD( "miot", 1000000, mk2_mos6530_interface )

	/* sound hardware */
	MCFG_SPEAKER_STANDARD_MONO("mono")

	MCFG_SOUND_ADD("dac", DAC, 0)
	MCFG_SOUND_ROUTE(ALL_OUTPUTS, "mono", 0.80)

	MCFG_TIMER_ADD_PERIODIC("led_timer", update_leds, attotime::from_hz(60))
MACHINE_CONFIG_END


ROM_START(mk2)
	ROM_REGION(0x10000,"maincpu",0)
	ROM_LOAD("024_1879", 0x0c00, 0x0400, CRC(4f28c443) SHA1(e33f8b7f38e54d7a6e0f0763f2328cc12cb0eade))
	ROM_LOAD("005_2179", 0x1000, 0x1000, CRC(6f10991b) SHA1(90cdc5a15d9ad813ad20410f21081c6e3e481812)) // chess mate 7.5
ROM_END

/***************************************************************************

  Game driver(s)

***************************************************************************/

/*
   port a
   0..7 led output
   0..6 keyboard input

   port b
    0..5 outputs
    0 speaker out
    6 as chipselect used!?
    7 interrupt out?

    c4, c5, keyboard polling
    c0, c1, c2, c3 led output

*/


/*    YEAR  NAME    PARENT  COMPAT  MACHINE INPUT   INIT    COMPANY   FULLNAME */
CONS( 1979,	mk2,	0,		0,		mk2,	mk2,	0,		"Quelle International",  "Chess Champion MK II", 0)
// second design sold (same computer/program?)
