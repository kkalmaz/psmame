class baraduke_state : public driver_device
{
public:
	baraduke_state(running_machine &machine, const driver_device_config_base &config)
		: driver_device(machine, config) { }

	int m_inputport_selected;
	int m_counter;
	UINT8 *m_textram;
	UINT8 *m_videoram;
	UINT8 *m_spriteram;
	tilemap_t *m_tx_tilemap;
	tilemap_t *m_bg_tilemap[2];
	int m_xscroll[2];
	int m_yscroll[2];
	int m_copy_sprites;
};


/*----------- defined in video/baraduke.c -----------*/

VIDEO_START( baraduke );
SCREEN_UPDATE( baraduke );
SCREEN_EOF( baraduke );
READ8_HANDLER( baraduke_videoram_r );
WRITE8_HANDLER( baraduke_videoram_w );
READ8_HANDLER( baraduke_textram_r );
WRITE8_HANDLER( baraduke_textram_w );
WRITE8_HANDLER( baraduke_scroll0_w );
WRITE8_HANDLER( baraduke_scroll1_w );
READ8_HANDLER( baraduke_spriteram_r );
WRITE8_HANDLER( baraduke_spriteram_w );
PALETTE_INIT( baraduke );
