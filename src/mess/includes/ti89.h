/*****************************************************************************
 *
 * includes/ti89.h
 *
 ****************************************************************************/

#ifndef TI89_H_
#define TI89_H_

#include "machine/intelfsh.h"

class ti68k_state : public driver_device
{
public:
	ti68k_state(running_machine &machine, const driver_device_config_base &config)
		: driver_device(machine, config),
		  m_maincpu(*this, "maincpu"),
		  m_flash(*this, "flash")
		{ }

	required_device<cpu_device> m_maincpu;
	required_device<sharp_unk128mbit_device> m_flash;

	// hardware versions
	enum { m_HW1=1, m_HW2, m_HW3, m_HW4 };

	// HW specifications
	UINT8 m_hw_version;
	bool m_flash_mem;
	UINT32 m_initial_pc;

	// keyboard
	UINT16 m_kb_mask;
	UINT8 m_on_key;

	// LCD
	UINT8 m_lcd_on;
	UINT32 m_lcd_base;
	UINT16 m_lcd_width;
	UINT16 m_lcd_height;
	UINT16 m_lcd_contrast;

	// I/O
	UINT16 m_io_hw1[0x10];
	UINT16 m_io_hw2[0x80];

	// Timer
	UINT8 m_timer_on;
	UINT8 m_timer_val;
	UINT16 m_timer_mask;

	virtual void machine_start();
	virtual void machine_reset();
	virtual bool screen_update(screen_device &screen, bitmap_t &bitmap, const rectangle &cliprect);

	UINT8 keypad_r (running_machine &machine);
	DECLARE_WRITE16_MEMBER ( ti68k_io_w );
	DECLARE_READ16_MEMBER ( ti68k_io_r );
	DECLARE_WRITE16_MEMBER ( ti68k_io2_w );
	DECLARE_READ16_MEMBER ( ti68k_io2_r );
	DECLARE_WRITE16_MEMBER ( flash_w );
	DECLARE_READ16_MEMBER ( flash_r );
	UINT64 m_timer;
};

#endif // TI89_H_
