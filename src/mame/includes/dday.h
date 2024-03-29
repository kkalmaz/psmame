/*************************************************************************

    D-Day

*************************************************************************/


class dday_state : public driver_device
{
public:
	dday_state(running_machine &machine, const driver_device_config_base &config)
		: driver_device(machine, config) { }

	/* memory pointers */
	UINT8 *        m_bgvideoram;
	UINT8 *        m_fgvideoram;
	UINT8 *        m_textvideoram;
	UINT8 *        m_colorram;

	/* video-related */
	tilemap_t        *m_fg_tilemap;
	tilemap_t        *m_bg_tilemap;
	tilemap_t        *m_text_tilemap;
	tilemap_t        *m_sl_tilemap;
	bitmap_t       *m_main_bitmap;
	int            m_control;
	int            m_sl_image;
	int            m_sl_enable;
	int            m_timer_value;

	/* devices */
	device_t *m_ay1;
};


/*----------- defined in video/dday.c -----------*/

PALETTE_INIT( dday );
VIDEO_START( dday );
SCREEN_UPDATE( dday );

WRITE8_HANDLER( dday_bgvideoram_w );
WRITE8_HANDLER( dday_fgvideoram_w );
WRITE8_HANDLER( dday_textvideoram_w );
WRITE8_HANDLER( dday_colorram_w );
READ8_HANDLER( dday_colorram_r );
WRITE8_HANDLER( dday_control_w );
WRITE8_HANDLER( dday_sl_control_w );
READ8_HANDLER( dday_countdown_timer_r );
