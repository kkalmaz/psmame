class snookr10_state : public driver_device
{
public:
	snookr10_state(running_machine &machine, const driver_device_config_base &config)
		: driver_device(machine, config) { }

	int m_outportl;
	int m_outporth;
	int m_bit0;
	int m_bit1;
	int m_bit2;
	int m_bit3;
	int m_bit4;
	int m_bit5;
	UINT8 *m_videoram;
	UINT8 *m_colorram;
	tilemap_t *m_bg_tilemap;
};


/*----------- defined in video/snookr10.c -----------*/

WRITE8_HANDLER( snookr10_videoram_w );
WRITE8_HANDLER( snookr10_colorram_w );
PALETTE_INIT( snookr10 );
PALETTE_INIT( apple10 );
VIDEO_START( snookr10 );
VIDEO_START( apple10 );
SCREEN_UPDATE( snookr10 );

