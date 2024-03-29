#ifndef __MC1000__
#define __MC1000__

#define ADDRESS_MAP_MODERN

#include "emu.h"
#include "cpu/z80/z80.h"
#include "imagedev/cassette.h"
#include "video/m6847.h"
#include "sound/ay8910.h"
#include "machine/ctronics.h"
#include "machine/rescap.h"
#include "machine/ram.h"

#define SCREEN_TAG		"screen"
#define Z80_TAG			"u13"
#define AY8910_TAG		"u21"
#define MC6845_TAG		"mc6845"
#define MC6847_TAG		"u19"
#define CASSETTE_TAG	"cassette"
#define CENTRONICS_TAG	"centronics"

#define MC1000_MC6845_VIDEORAM_SIZE		0x800
#define MC1000_MC6847_VIDEORAM_SIZE		0x1800

class mc1000_state : public driver_device
{
public:
	mc1000_state(running_machine &machine, const driver_device_config_base &config)
		: driver_device(machine, config),
		  m_maincpu(*this, Z80_TAG),
		  m_vdg(*this, MC6847_TAG),
		  m_crtc(*this, MC6845_TAG),
		  m_centronics(*this, CENTRONICS_TAG),
		  m_cassette(*this, CASSETTE_TAG),
		  m_ram(*this, RAM_TAG)
	{ }

	required_device<cpu_device> m_maincpu;
	required_device<device_t> m_vdg;
	optional_device<device_t> m_crtc;
	required_device<device_t> m_centronics;
	required_device<device_t> m_cassette;
	required_device<device_t> m_ram;

	virtual void machine_start();
	virtual void machine_reset();

	virtual bool screen_update(screen_device &screen, bitmap_t &bitmap, const rectangle &cliprect);

	DECLARE_READ8_MEMBER( printer_r );
	DECLARE_WRITE8_MEMBER( printer_w );
	DECLARE_WRITE8_MEMBER( mc6845_ctrl_w );
	DECLARE_WRITE8_MEMBER( mc6847_attr_w );
	DECLARE_WRITE_LINE_MEMBER( fs_w );
	DECLARE_WRITE_LINE_MEMBER( hs_w );
	DECLARE_READ8_MEMBER( videoram_r );
	DECLARE_WRITE8_MEMBER( keylatch_w );
	DECLARE_READ8_MEMBER( keydata_r );
	
	void bankswitch();
	
	/* cpu state */
	int m_ne555_int;

	/* memory state */
	int m_rom0000;
	int m_mc6845_bank;
	int m_mc6847_bank;

	/* keyboard state */
	int m_keylatch;

	/* video state */
	int m_hsync;
	int m_vsync;
	UINT8 *m_mc6845_video_ram;
	UINT8 *m_mc6847_video_ram;
	UINT8 m_mc6847_attr;
};

#endif
