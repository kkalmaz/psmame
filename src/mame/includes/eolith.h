class eolith_state : public driver_device
{
public:
	eolith_state(running_machine &machine, const driver_device_config_base &config)
		: driver_device(machine, config) { }

	int m_coin_counter_bit;
	int m_buffer;
	UINT32 *m_vram;
};


/*----------- defined in video/eolith.c -----------*/

READ32_HANDLER( eolith_vram_r );
WRITE32_HANDLER( eolith_vram_w );
VIDEO_START( eolith );
SCREEN_UPDATE( eolith );
