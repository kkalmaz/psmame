/*************************************************************************

    Sega vector hardware

*************************************************************************/

#include "machine/segag80.h"

class segag80v_state : public driver_device
{
public:
	segag80v_state(running_machine &machine, const driver_device_config_base &config)
		: driver_device(machine, config) { }

	UINT8 *m_mainram;
	UINT8 m_has_usb;
	UINT8 m_mult_data[2];
	UINT16 m_mult_result;
	UINT8 m_spinner_select;
	UINT8 m_spinner_sign;
	UINT8 m_spinner_count;
	segag80_decrypt_func m_decrypt;
	UINT8 *m_vectorram;
	size_t m_vectorram_size;
	int m_min_x;
	int m_min_y;
};


/*----------- defined in audio/segag80v.c -----------*/

WRITE8_HANDLER( elim1_sh_w );
WRITE8_HANDLER( elim2_sh_w );
WRITE8_HANDLER( spacfury1_sh_w );
WRITE8_HANDLER( spacfury2_sh_w );
WRITE8_HANDLER( zektor1_sh_w );
WRITE8_HANDLER( zektor2_sh_w );


/*----------- defined in video/segag80v.c -----------*/

VIDEO_START( segag80v );
SCREEN_UPDATE( segag80v );
