#pragma once

#ifndef __PROF180X__
#define __PROF180X__

#define HD64180_TAG				"hd64180"
#define FDC9268_TAG				"fdc9268"
#define FDC9229_TAG				"fdc9229"
#define MK3835_TAG				"mk3835"
#define SCREEN_TAG				"screen"
#define CENTRONICS_TAG			"centronics"

class prof180x_state : public driver_device
{
public:
	prof180x_state(running_machine &machine, const driver_device_config_base &config)
		: driver_device(machine, config) { }

	virtual void machine_start();
	virtual void machine_reset();

	DECLARE_WRITE8_MEMBER( flr_w );
	DECLARE_READ8_MEMBER( status0_r );
	DECLARE_READ8_MEMBER( status1_r );
	DECLARE_READ8_MEMBER( status_r );

	void bankswitch();
	void ls259_w(int flag, int value);

	int m_c0;
	int m_c1;
	int m_c2;
	int m_mm0;
	int m_mm1;
};

#endif
