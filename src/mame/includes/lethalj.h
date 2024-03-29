/*************************************************************************

    The Game Room Lethal Justice hardware

**************************************************************************/

class lethalj_state : public driver_device
{
public:
	lethalj_state(running_machine &machine, const driver_device_config_base &config)
		: driver_device(machine, config) { }

	UINT16 m_blitter_data[8];
	UINT16 *m_screenram;
	UINT8 m_vispage;
	UINT16 *m_blitter_base;
	int m_blitter_rows;
	UINT16 m_gunx;
	UINT16 m_guny;
	UINT8 m_blank_palette;
};


/*----------- defined in video/lethalj.c -----------*/

READ16_HANDLER( lethalj_gun_r );

VIDEO_START( lethalj );

WRITE16_HANDLER( lethalj_blitter_w );

void lethalj_scanline_update(screen_device &screen, bitmap_t *bitmap, int scanline, const tms34010_display_params *params);
