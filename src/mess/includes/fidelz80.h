/***************************************************************************

        fidelz80.h

****************************************************************************/

#pragma once

#ifndef _FIDELZ80_H_
#define _FIDELZ80_H_

class fidelz80_state : public driver_device
{
public:
	fidelz80_state(running_machine &machine, const driver_device_config_base &config)
		: driver_device(machine, config),
		  m_maincpu(*this, "maincpu"),
		  m_speech(*this, "speech"),
		  m_beep(*this, "beep"),
		  m_i8041(*this, "mcu"),
		  m_i8243(*this, "i8243")
		{ }

	required_device<cpu_device> m_maincpu;
	optional_device<device_t> m_speech;
	optional_device<device_t> m_beep;
	optional_device<cpu_device> m_i8041;
	optional_device<i8243_device> m_i8243;

	UINT8 m_kp_matrix;			// keypad matrix
	UINT8 m_led_data;			// data for the two individual leds, in 0bxxxx12xx format
	UINT8 m_led_selected;		// 5 bit selects for 7 seg leds and for common other leds, bits are (7seg leds are 0 1 2 3, common other leds are C) 0bxx3210xc
	UINT16 m_digit_data;		// data for seg leds
	UINT8 m_digit_line_status[4];	// prevent overwrite of m_digit_data

	virtual void machine_reset();

	void update_display(running_machine &machine);
	DECLARE_READ8_MEMBER( fidelz80_portc_r );
	DECLARE_WRITE8_MEMBER( fidelz80_portb_w );
	DECLARE_WRITE8_MEMBER( fidelz80_portc_w );
	DECLARE_READ8_MEMBER( cc10_portb_r );
	DECLARE_WRITE8_MEMBER( cc10_porta_w );
	DECLARE_READ8_MEMBER( vcc_portb_r );
	DECLARE_WRITE8_MEMBER( vcc_porta_w );
	DECLARE_WRITE8_MEMBER( abc_speech_w );
	DECLARE_WRITE8_MEMBER(kp_matrix_w);
	DECLARE_READ8_MEMBER(unknown_r);
	DECLARE_READ8_MEMBER(exp_i8243_p2_r);
	DECLARE_WRITE8_MEMBER(exp_i8243_p2_w);
	DECLARE_READ8_MEMBER(rand_r);
	DECLARE_WRITE8_MEMBER(mcu_data_w);
	DECLARE_WRITE8_MEMBER(mcu_command_w);
	DECLARE_READ8_MEMBER(mcu_data_r);
	DECLARE_READ8_MEMBER(mcu_status_r);
};


#endif	// _FIDELZ80_H_
