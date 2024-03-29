/*****************************************************************************
 *
 * includes/advision.h
 *
 ****************************************************************************/

#ifndef __ADVISION__
#define __ADVISION__

#define SCREEN_TAG	"screen"
#define I8048_TAG	"i8048"
#define COP411_TAG	"cop411"

class advision_state : public driver_device
{
public:
	advision_state(running_machine &machine, const driver_device_config_base &config)
		: driver_device(machine, config),
		  m_maincpu(*this, I8048_TAG),
		  m_soundcpu(*this, COP411_TAG),
		  m_dac(*this, "dac")
	{ }

	required_device<cpu_device> m_maincpu;
	required_device<cpu_device> m_soundcpu;
	required_device<device_t> m_dac;

	virtual void machine_start();
	virtual void machine_reset();

	virtual void video_start();
	virtual bool screen_update(screen_device &screen, bitmap_t &bitmap, const rectangle &cliprect);

	void update_dac();
	void vh_write(int data);
	void vh_update(int x);

	DECLARE_READ8_MEMBER( ext_ram_r );
	DECLARE_WRITE8_MEMBER( ext_ram_w );
	DECLARE_READ8_MEMBER( controller_r );
	DECLARE_WRITE8_MEMBER( bankswitch_w );
	DECLARE_WRITE8_MEMBER( av_control_w );
	DECLARE_READ8_MEMBER( vsync_r );
	DECLARE_READ8_MEMBER( sound_cmd_r );
	DECLARE_WRITE8_MEMBER( sound_g_w );
	DECLARE_WRITE8_MEMBER( sound_d_w );

	/* external RAM state */
	UINT8 *m_ext_ram;
	int m_rambank;

	/* video state */
	int m_frame_count;
	int m_frame_start;
	int m_video_enable;
	int m_video_bank;
	int m_video_hpos;
	UINT8 m_led_latch[8];
	UINT8 *m_display;

	/* sound state */
	int m_sound_cmd;
	int m_sound_d;
	int m_sound_g;
};

/*----------- defined in video/advision.c -----------*/

PALETTE_INIT( advision );

#endif
