/**************************************************************************************

    Counter Steer                   (c) 1985 Data East Corporation
    Zero Target                     (c) 1985 Data East Corporation
    Gekitsui Oh                     (c) 1985 Data East Corporation

    Emulation by Bryan McPhail, mish@tendril.co.uk
    Improvements by Pierpaolo Prazzoli, David Haywood, Angelo Salese

    todo:
    finish
        - correct roz rotation;
        - make cntsteer work, comms looks awkward and probably different than Zero Target;
        - flip screen support;
        - according to a side-by-side test, sound should be "darker" by some octaves,
          likely that a sound filter is needed;
    cleanup
        - split into driver/video;

    note: To boot cntsteer, set a CPU #1 breakpoint on c225 and then 'do pc=c230'.
          Protection maybe?

***************************************************************************************/

#include "emu.h"
#include "cpu/m6502/m6502.h"
#include "cpu/m6809/m6809.h"
#include "sound/ay8910.h"
#include "sound/dac.h"


class cntsteer_state : public driver_device
{
public:
	cntsteer_state(running_machine &machine, const driver_device_config_base &config)
		: driver_device(machine, config) { }

	/* memory pointers */
	UINT8 *  m_videoram;
	UINT8 *  m_videoram2;
	UINT8 *  m_colorram;
	UINT8 *  m_spriteram;

	/* video-related */
	tilemap_t  *m_bg_tilemap;
	tilemap_t  *m_fg_tilemap;
	int      m_bg_bank;
	int      m_bg_color_bank;
	int      m_flipscreen;
	int      m_scrolly;
	int      m_scrolly_hi;
	int      m_scrollx;
	int      m_scrollx_hi;
	int      m_rotation_x;
	int		 m_rotation_sign;
	int      m_disable_roz;

	/* misc */
	int      m_nmimask;	// zerotrgt only

	/* devices */
	device_t *m_maincpu;
	device_t *m_audiocpu;
	device_t *m_subcpu;
};


static PALETTE_INIT( zerotrgt )
{
	int i;
	for (i = 0; i < machine.total_colors(); i++)
	{
		int bit0, bit1, bit2, r, g, b;

		/* red component */
		bit0 = (color_prom[i] >> 0) & 0x01;
		bit1 = (color_prom[i] >> 1) & 0x01;
		bit2 = (color_prom[i] >> 2) & 0x01;
		g = (0x21 * bit0 + 0x47 * bit1 + 0x97 * bit2);
		/* green component */
		bit0 = (color_prom[i] >> 4) & 0x01;
		bit1 = (color_prom[i] >> 5) & 0x01;
		bit2 = (color_prom[i] >> 6) & 0x01;
		r = (0x21 * bit0 + 0x47 * bit1 + 0x97 * bit2);
		/* blue component */
		bit0 = (color_prom[i + 256] >> 0) & 0x01;
		bit1 = (color_prom[i + 256] >> 1) & 0x01;
		bit2 = (color_prom[i + 256] >> 2) & 0x01;
		b = (0x21 * bit0 + 0x47 * bit1 + 0x97 * bit2);

		palette_set_color(machine, i, MAKE_RGB(r,g,b));
	}
}

static TILE_GET_INFO( get_bg_tile_info )
{
	cntsteer_state *state = machine.driver_data<cntsteer_state>();
	int code = state->m_videoram2[tile_index];

	SET_TILE_INFO(2, code + state->m_bg_bank, state->m_bg_color_bank, 0);
}

static TILE_GET_INFO( get_fg_tile_info )
{
	cntsteer_state *state = machine.driver_data<cntsteer_state>();
	int code = state->m_videoram[tile_index];
	int attr = state->m_colorram[tile_index];

	code |= (attr & 0x01) << 8;

	SET_TILE_INFO(0, code, 0x30 + ((attr & 0x78) >> 3), 0);
}

static VIDEO_START( cntsteer )
{
	cntsteer_state *state = machine.driver_data<cntsteer_state>();
	state->m_bg_tilemap = tilemap_create(machine, get_bg_tile_info, tilemap_scan_cols, 16, 16, 64, 64);
	state->m_fg_tilemap = tilemap_create(machine, get_fg_tile_info, tilemap_scan_rows_flip_x, 8, 8, 32, 32);

	tilemap_set_transparent_pen(state->m_fg_tilemap, 0);

	//tilemap_set_flip(state->m_bg_tilemap, TILEMAP_FLIPX | TILEMAP_FLIPY);
}

static VIDEO_START( zerotrgt )
{
	cntsteer_state *state = machine.driver_data<cntsteer_state>();
	state->m_bg_tilemap = tilemap_create(machine, get_bg_tile_info, tilemap_scan_rows, 16, 16, 64, 64);
	state->m_fg_tilemap = tilemap_create(machine, get_fg_tile_info, tilemap_scan_rows_flip_x, 8, 8, 32, 32);

	tilemap_set_transparent_pen(state->m_fg_tilemap, 0);

	//tilemap_set_flip(state->m_bg_tilemap, TILEMAP_FLIPX | TILEMAP_FLIPY);
}

/*
Sprite list:

[0] xxxx xxxx Y attribute
[1] xx-- ---- sprite number bank
    --x- x--- color number
    ---x ---- double-height attribute
    ---- -x-- flip x (active low)
    ---- --x- flip y
    ---- ---x draw sprite flag (active low)
[2] xxxx xxxx X attribute
[3] xxxx xxxx sprite number
*/
static void zerotrgt_draw_sprites( running_machine &machine, bitmap_t *bitmap, const rectangle *cliprect )
{
	cntsteer_state *state = machine.driver_data<cntsteer_state>();
	int offs;

	for (offs = 0; offs < 0x200; offs += 4)
	{
		int multi, fx, fy, sx, sy, code, color;

		if ((state->m_spriteram[offs + 1] & 1) == 1)
			continue;

		code = state->m_spriteram[offs + 3] + ((state->m_spriteram[offs + 1] & 0xc0) << 2);
		sx = (state->m_spriteram[offs + 2]);
		sy = 0xf0 - state->m_spriteram[offs];
		color = 0x10 + ((state->m_spriteram[offs + 1] & 0x20) >> 4) + ((state->m_spriteram[offs + 1] & 0x8)>>3);

		fx = !(state->m_spriteram[offs + 1] & 0x04);
		fy = (state->m_spriteram[offs + 1] & 0x02);

		multi = state->m_spriteram[offs + 1] & 0x10;

		if (state->m_flipscreen)
		{
			sy = 240 - sy;
			sx = 240 - sx;
			if (fx) fx = 0;
			else fx = 1;
			//sy2 = sy - 16;
		}

		if (multi)
		{
			if (fy)
			{
				drawgfx_transpen(bitmap, cliprect, machine.gfx[1], code, color, fx, fy, sx, sy, 0);
				drawgfx_transpen(bitmap, cliprect, machine.gfx[1], code + 1, color, fx, fy, sx, sy - 16, 0);
			}
			else
			{
				drawgfx_transpen(bitmap, cliprect, machine.gfx[1], code, color, fx, fy, sx, sy - 16, 0);
				drawgfx_transpen(bitmap, cliprect, machine.gfx[1], code + 1, color, fx, fy, sx, sy, 0);
			}
		}
		else
			drawgfx_transpen(bitmap, cliprect, machine.gfx[1], code, color, fx, fy, sx, sy, 0);
	}
}

/*
[00] --x- ---- magnify
     ---x ---- double height
     ---- -x-- flipy

[80] -xxx ---- palette entry
     ---- --xx tile bank
*/

static void cntsteer_draw_sprites( running_machine &machine, bitmap_t *bitmap, const rectangle *cliprect )
{
	cntsteer_state *state = machine.driver_data<cntsteer_state>();
	int offs;

	for (offs = 0; offs < 0x80; offs += 4)
	{
		int multi, fx, fy, sx, sy, code, color;

		if ((state->m_spriteram[offs + 0] & 1) == 0)
			continue;

		code = state->m_spriteram[offs + 1] + ((state->m_spriteram[offs + 0x80] & 0x03) << 8);
		sx = 0x100 - state->m_spriteram[offs + 3];
		sy = 0x100 - state->m_spriteram[offs + 2];
		color = 0x10 + ((state->m_spriteram[offs + 0x80] & 0x70) >> 4);

		fx = (state->m_spriteram[offs + 0] & 0x04);
		fy = (state->m_spriteram[offs + 0] & 0x02);

		multi = state->m_spriteram[offs + 0] & 0x10;

		if (state->m_flipscreen)
		{
			sy = 240 - sy;
			sx = 240 - sx;
			if (fx) fx = 0;
			else fx = 1;
			//sy2 = sy - 16;
		}

		if (multi)
		{
			if (fy)
			{
				drawgfx_transpen(bitmap, cliprect, machine.gfx[1], code, color, fx, fy, sx, sy, 0);
				drawgfx_transpen(bitmap, cliprect, machine.gfx[1], code + 1, color, fx, fy, sx, sy - 16, 0);
			}
			else
			{
				drawgfx_transpen(bitmap, cliprect, machine.gfx[1], code, color, fx, fy, sx, sy - 16, 0);
				drawgfx_transpen(bitmap, cliprect, machine.gfx[1], code + 1, color, fx, fy, sx, sy, 0);
			}
		}
		else
			drawgfx_transpen(bitmap, cliprect, machine.gfx[1], code, color, fx, fy, sx, sy, 0);
	}
}

static SCREEN_UPDATE( zerotrgt )
{
	cntsteer_state *state = screen->machine().driver_data<cntsteer_state>();

	if (state->m_disable_roz)
		bitmap_fill(bitmap, cliprect, screen->machine().pens[8 * state->m_bg_color_bank]);
	else
	{
		int p1, p2, p3, p4;
		int rot_val, x, y;
		rot_val = state->m_rotation_sign ? (-state->m_rotation_x) : (state->m_rotation_x);

//      popmessage("%d %02x %02x", rot_val, state->m_rotation_sign, state->m_rotation_x);

		if (rot_val > 90) { rot_val = 90; }
		if (rot_val < -90) { rot_val = -90; }

		/*
        (u, v) = (a + cx + dy, b - dx + cy) when (x, y)=screen and (u, v) = tilemap
        */
		/*
             1
        0----|----0
            -1
             0
        0----|----1
             0
        */
		/*65536*z*cos(a), 65536*z*sin(a), -65536*z*sin(a), 65536*z*cos(a)*/
		p1 = -65536 * 1 * cos(2 * M_PI * (rot_val) / 1024);
		p2 = -65536 * 1 * sin(2 * M_PI * (rot_val) / 1024);
		p3 = 65536 * 1 * sin(2 * M_PI * (rot_val) / 1024);
		p4 = -65536 * 1 * cos(2 * M_PI * (rot_val) / 1024);

		x = -256 - (state->m_scrollx | state->m_scrollx_hi);
		y = 256 + (state->m_scrolly | state->m_scrolly_hi);

		tilemap_draw_roz(bitmap, cliprect, state->m_bg_tilemap,
						(x << 16), (y << 16),
						p1, p2,
						p3, p4,
						1,
						0, 0);
	}

	zerotrgt_draw_sprites(screen->machine(), bitmap, cliprect);
	tilemap_draw(bitmap, cliprect, state->m_fg_tilemap, 0, 0);

	return 0;
}

static SCREEN_UPDATE( cntsteer )
{
	cntsteer_state *state = screen->machine().driver_data<cntsteer_state>();

	if (state->m_disable_roz)
		bitmap_fill(bitmap, cliprect, screen->machine().pens[8 * state->m_bg_color_bank]);
	else
	{
		int p1, p2, p3, p4;
		int rot_val, x, y;

		rot_val = (state->m_rotation_x) | ((state->m_rotation_sign & 3) << 8);
		rot_val = (state->m_rotation_sign & 4) ? (rot_val) : (-rot_val);
//      popmessage("%d %02x %02x", rot_val, state->m_rotation_sign, state->m_rotation_x);

		/*
        (u, v) = (a + cx + dy, b - dx + cy) when (x, y)=screen and (u, v) = tilemap
        */
		/*
             1
        0----|----0
            -1
             0
        0----|----1
             0
        */
		/*65536*z*cos(a), 65536*z*sin(a), -65536*z*sin(a), 65536*z*cos(a)*/
		p1 = -65536 * 1 * cos(2 * M_PI * (rot_val) / 1024);
		p2 = -65536 * 1 * sin(2 * M_PI * (rot_val) / 1024);
		p3 = 65536 * 1 * sin(2 * M_PI * (rot_val) / 1024);
		p4 = -65536 * 1 * cos(2 * M_PI * (rot_val) / 1024);

		x = 256 + (state->m_scrollx | state->m_scrollx_hi);
		y = 256 - (state->m_scrolly | state->m_scrolly_hi);

		tilemap_draw_roz(bitmap, cliprect, state->m_bg_tilemap,
						(x << 16), (y << 16),
						p1, p2,
						p3, p4,
						1,
						0, 0);
	}

	cntsteer_draw_sprites(screen->machine(), bitmap, cliprect);
	tilemap_draw(bitmap, cliprect, state->m_fg_tilemap, 0, 0);

	return 0;
}

/*
[0] = scroll y
[1] = scroll x
[2] = -x-- ---- disable roz layer (Used when you lose a life / start a new play)
      --xx ---- bg bank
      ---- -xxx bg color bank
[3] = xx-- ---- high word scrolling x bits
      --xx ---- high word scrolling y bits
      ---- -x-- flip screen bit (inverted)
      ---- ---x rotation sign (landscape should be turning right (0) == / , turning left (1) == \)
[4] = xxxx xxxx rotation factor?
*/
static WRITE8_HANDLER(zerotrgt_vregs_w)
{
	cntsteer_state *state = space->machine().driver_data<cntsteer_state>();
//  static UINT8 test[5];

//  test[offset] = data;
//    popmessage("%02x %02x %02x %02x %02x",test[0],test[1],test[2],test[3],test[4]);

	switch (offset)
	{
		case 0:	state->m_scrolly = data; break;
		case 1:	state->m_scrollx = data; break;
		case 2:	state->m_bg_bank = (data & 0x30) << 4;
				state->m_bg_color_bank = (data & 7);
				state->m_disable_roz = (data & 0x40);
				tilemap_mark_all_tiles_dirty(state->m_bg_tilemap);
				break;
		case 3:	state->m_rotation_sign = (data & 1);
				flip_screen_set(space->machine(), !(data & 4));
				state->m_scrolly_hi = (data & 0x30) << 4;
				state->m_scrollx_hi = (data & 0xc0) << 2;
				break;
		case 4:	state->m_rotation_x = data; break;
	}
}

static WRITE8_HANDLER(cntsteer_vregs_w)
{
	cntsteer_state *state = space->machine().driver_data<cntsteer_state>();
//  static UINT8 test[5];

//  test[offset] = data;
//   popmessage("%02x %02x %02x %02x %02x",test[0],test[1],test[2],test[3],test[4]);

	switch(offset)
	{
		case 0:	state->m_scrolly = data; break;
		case 1:	state->m_scrollx = data; break;
		case 2:	state->m_bg_bank = (data & 0x01) << 8;
				state->m_bg_color_bank = (data & 6) >> 1;
				tilemap_mark_all_tiles_dirty(state->m_bg_tilemap);
				break;
		case 3:	state->m_rotation_sign = (data & 7);
				state->m_disable_roz = (~data & 0x08);
				state->m_scrolly_hi = (data & 0x30) << 4;
				state->m_scrollx_hi = (data & 0xc0) << 2;
				break;
		case 4:	state->m_rotation_x = data; break;
	}
}

static WRITE8_HANDLER( cntsteer_foreground_vram_w )
{
	cntsteer_state *state = space->machine().driver_data<cntsteer_state>();
	state->m_videoram[offset] = data;
	tilemap_mark_tile_dirty(state->m_fg_tilemap, offset);
}

static WRITE8_HANDLER( cntsteer_foreground_attr_w )
{
	cntsteer_state *state = space->machine().driver_data<cntsteer_state>();
	state->m_colorram[offset] = data;
	tilemap_mark_tile_dirty(state->m_fg_tilemap, offset);
}

static WRITE8_HANDLER( cntsteer_background_w )
{
	cntsteer_state *state = space->machine().driver_data<cntsteer_state>();
	state->m_videoram2[offset] = data;
	tilemap_mark_tile_dirty(state->m_bg_tilemap, offset);
}

/*************************************
 *
 * CPU comms
 *
 *************************************/

static WRITE8_HANDLER( gekitsui_sub_irq_ack )
{
	cntsteer_state *state = space->machine().driver_data<cntsteer_state>();
	device_set_input_line(state->m_subcpu, M6809_IRQ_LINE, CLEAR_LINE);
}

static WRITE8_HANDLER( cntsteer_sound_w )
{
	cntsteer_state *state = space->machine().driver_data<cntsteer_state>();
	soundlatch_w(space, 0, data);
	device_set_input_line(state->m_audiocpu, 0, HOLD_LINE);
}

static WRITE8_HANDLER( zerotrgt_ctrl_w )
{
	cntsteer_state *state = space->machine().driver_data<cntsteer_state>();
	/*TODO: check this.*/
	logerror("CTRL: %04x: %04x: %04x\n", cpu_get_pc(&space->device()), offset, data);
//  if (offset == 0) device_set_input_line(state->m_subcpu, INPUT_LINE_RESET, ASSERT_LINE);

	// Wrong - bits 0 & 1 used on this
	if (offset == 1) device_set_input_line(state->m_subcpu, M6809_IRQ_LINE, ASSERT_LINE);
//  if (offset == 2) device_set_input_line(state->m_subcpu, INPUT_LINE_RESET, CLEAR_LINE);
}

static WRITE8_HANDLER( cntsteer_sub_irq_w )
{
	cntsteer_state *state = space->machine().driver_data<cntsteer_state>();
	device_set_input_line(state->m_subcpu, M6809_IRQ_LINE, ASSERT_LINE);
//  printf("%02x IRQ\n", data);
}

static WRITE8_HANDLER( cntsteer_sub_nmi_w )
{
//  if (data)
//  device_set_input_line(state->m_subcpu, INPUT_LINE_NMI, PULSE_LINE);
//  popmessage("%02x", data);
}

static WRITE8_HANDLER( cntsteer_main_irq_w )
{
	cntsteer_state *state = space->machine().driver_data<cntsteer_state>();
	device_set_input_line(state->m_maincpu, M6809_IRQ_LINE, HOLD_LINE);
}

/* Convert weird input handling with MAME standards.*/
static READ8_HANDLER( cntsteer_adx_r )
{
	UINT8 res = 0, adx_val;
	adx_val = input_port_read(space->machine(), "AN_STEERING");

	if (adx_val >= 0x70 && adx_val <= 0x90)
		res = 0xff;
	else if (adx_val > 0x90)
	{
		if (adx_val > 0x90 && adx_val <= 0xb0)
			res = 0xfe;
		else if (adx_val > 0xb0 && adx_val <= 0xd0)
			res = 0xfc;
		else if (adx_val > 0xd0 && adx_val <= 0xf0)
			res = 0xf8;
		else if (adx_val > 0xf0)
			res = 0xf0;
	}
	else
	{
		if (adx_val >= 0x50 && adx_val < 0x70)
			res = 0xef;
		else if (adx_val >= 0x30 && adx_val < 0x50)
			res = 0xcf;
		else if (adx_val >= 0x10 && adx_val < 0x30)
			res = 0x8f;
		else if (adx_val < 0x10)
			res = 0x0f;
	}
	//popmessage("%02x", adx_val);
	return res;
}

/***************************************************************************/

static ADDRESS_MAP_START( gekitsui_cpu1_map, AS_PROGRAM, 8 )
	AM_RANGE(0x0000, 0x0fff) AM_RAM AM_SHARE("share1")
	AM_RANGE(0x1000, 0x11ff) AM_RAM AM_BASE_MEMBER(cntsteer_state, m_spriteram)
	AM_RANGE(0x1200, 0x1fff) AM_RAM
	AM_RANGE(0x2000, 0x23ff) AM_RAM_WRITE(cntsteer_foreground_vram_w) AM_BASE_MEMBER(cntsteer_state, m_videoram)
	AM_RANGE(0x2400, 0x27ff) AM_RAM_WRITE(cntsteer_foreground_attr_w) AM_BASE_MEMBER(cntsteer_state, m_colorram)
	AM_RANGE(0x3000, 0x3003) AM_WRITE(zerotrgt_ctrl_w)
	AM_RANGE(0x8000, 0xffff) AM_ROM
ADDRESS_MAP_END

static ADDRESS_MAP_START( gekitsui_cpu2_map, AS_PROGRAM, 8 )
	AM_RANGE(0x0000, 0x0fff) AM_RAM AM_SHARE("share1")
	AM_RANGE(0x1000, 0x1fff) AM_RAM_WRITE(cntsteer_background_w) AM_BASE_MEMBER(cntsteer_state, m_videoram2)
	AM_RANGE(0x3000, 0x3000) AM_READ_PORT("DSW0")
	AM_RANGE(0x3001, 0x3001) AM_READ_PORT("P2")
	AM_RANGE(0x3002, 0x3002) AM_READ_PORT("P1")
	AM_RANGE(0x3003, 0x3003) AM_READ_PORT("COINS")
	AM_RANGE(0x3000, 0x3004) AM_WRITE(zerotrgt_vregs_w)
	AM_RANGE(0x3005, 0x3005) AM_WRITE(gekitsui_sub_irq_ack)
	AM_RANGE(0x3007, 0x3007) AM_WRITE(cntsteer_sound_w)
	AM_RANGE(0x4000, 0xffff) AM_ROM
ADDRESS_MAP_END

static ADDRESS_MAP_START( cntsteer_cpu1_map, AS_PROGRAM, 8 )
	AM_RANGE(0x0000, 0x0fff) AM_RAM AM_SHARE("share1")
	AM_RANGE(0x1000, 0x11ff) AM_RAM AM_BASE_MEMBER(cntsteer_state, m_spriteram)
	AM_RANGE(0x2000, 0x23ff) AM_RAM_WRITE(cntsteer_foreground_vram_w) AM_BASE_MEMBER(cntsteer_state, m_videoram)
	AM_RANGE(0x2400, 0x27ff) AM_RAM_WRITE(cntsteer_foreground_attr_w) AM_BASE_MEMBER(cntsteer_state, m_colorram)
	AM_RANGE(0x3000, 0x3000) AM_WRITE(cntsteer_sub_nmi_w)
	AM_RANGE(0x3001, 0x3001) AM_WRITE(cntsteer_sub_irq_w)
	AM_RANGE(0x8000, 0xffff) AM_ROM
ADDRESS_MAP_END

static ADDRESS_MAP_START( cntsteer_cpu2_map, AS_PROGRAM, 8 )
	AM_RANGE(0x0000, 0x0fff) AM_RAM AM_SHARE("share1")
	AM_RANGE(0x1000, 0x1fff) AM_RAM_WRITE(cntsteer_background_w) AM_BASE_MEMBER(cntsteer_state, m_videoram2) AM_SHARE("share3")
	AM_RANGE(0x2000, 0x2fff) AM_RAM_WRITE(cntsteer_background_w) AM_SHARE("share3")
	AM_RANGE(0x3000, 0x3000) AM_READ_PORT("DSW0")
	AM_RANGE(0x3001, 0x3001) AM_READ(cntsteer_adx_r)
	AM_RANGE(0x3002, 0x3002) AM_READ_PORT("P1")
	AM_RANGE(0x3003, 0x3003) AM_READ_PORT("COINS")
	AM_RANGE(0x3000, 0x3004) AM_WRITE(cntsteer_vregs_w)
	AM_RANGE(0x3005, 0x3005) AM_WRITE(gekitsui_sub_irq_ack)
	AM_RANGE(0x3006, 0x3006) AM_WRITE(cntsteer_main_irq_w)
	AM_RANGE(0x3007, 0x3007) AM_WRITE(cntsteer_sound_w)
	AM_RANGE(0x3007, 0x3007) AM_READNOP //m6809 bug.
	AM_RANGE(0x4000, 0xffff) AM_ROM
ADDRESS_MAP_END

/***************************************************************************/

static WRITE8_HANDLER( nmimask_w )
{
	cntsteer_state *state = space->machine().driver_data<cntsteer_state>();
	state->m_nmimask = data & 0x80;
}

static INTERRUPT_GEN ( sound_interrupt )
{
	cntsteer_state *state = device->machine().driver_data<cntsteer_state>();
	if (!state->m_nmimask)
		device_set_input_line(device, INPUT_LINE_NMI, PULSE_LINE);
}

static ADDRESS_MAP_START( sound_map, AS_PROGRAM, 8 )
	AM_RANGE(0x0000, 0x01ff) AM_RAM
//  AM_RANGE(0x1000, 0x1000) AM_WRITE(nmiack_w)
	AM_RANGE(0x2000, 0x2000) AM_DEVWRITE("ay1", ay8910_data_w)
	AM_RANGE(0x4000, 0x4000) AM_DEVWRITE("ay1", ay8910_address_w)
	AM_RANGE(0x6000, 0x6000) AM_DEVWRITE("ay2", ay8910_data_w)
	AM_RANGE(0x8000, 0x8000) AM_DEVWRITE("ay2", ay8910_address_w)
	AM_RANGE(0xa000, 0xa000) AM_READ(soundlatch_r)
	AM_RANGE(0xd000, 0xd000) AM_WRITE(nmimask_w)
	AM_RANGE(0xe000, 0xffff) AM_ROM
ADDRESS_MAP_END


/***************************************************************************/

static INPUT_PORTS_START( zerotrgt )
	PORT_START("DSW0")
	PORT_DIPNAME( 0x03, 0x03, DEF_STR( Coin_A ) ) PORT_DIPLOCATION("SW1:1,2")
	PORT_DIPSETTING(    0x03, DEF_STR( 1C_2C ) )
	PORT_DIPSETTING(    0x02, DEF_STR( 1C_3C ) )
	PORT_DIPSETTING(    0x01, DEF_STR( 1C_4C ) )
	PORT_DIPSETTING(    0x00, DEF_STR( 1C_6C ) )
	PORT_DIPNAME( 0x0c, 0x0c, DEF_STR( Coin_B ) ) PORT_DIPLOCATION("SW1:3,4")
	PORT_DIPSETTING(    0x00, DEF_STR( 4C_1C ) )
	PORT_DIPSETTING(    0x04, DEF_STR( 3C_1C ) )
	PORT_DIPSETTING(    0x08, DEF_STR( 2C_1C ) )
	PORT_DIPSETTING(    0x0c, DEF_STR( 1C_1C ) )
	PORT_DIPNAME( 0x10, 0x10, DEF_STR( Cabinet ) ) PORT_DIPLOCATION("SW1:5")
	PORT_DIPSETTING(    0x00, DEF_STR( Upright ) )
	PORT_DIPSETTING(    0x10, DEF_STR( Cocktail ) )
	PORT_DIPNAME( 0x20, 0x20, DEF_STR( Difficulty ) ) PORT_DIPLOCATION("SW1:6")
	PORT_DIPSETTING(    0x20, DEF_STR( Normal ) )
	PORT_DIPSETTING(    0x00, DEF_STR( Hard ) )
	PORT_DIPNAME( 0x40, 0x40, DEF_STR( Lives ) ) PORT_DIPLOCATION("SW1:7")
	PORT_DIPSETTING(    0x40, "3" )
	PORT_DIPSETTING(    0x00, "5" )
	PORT_DIPNAME( 0x80, 0x80, DEF_STR( Bonus_Life ) ) PORT_DIPLOCATION("SW1:8")
	PORT_DIPSETTING(    0x80, "30K only" )
	PORT_DIPSETTING(    0x00, "30K and 100K" )

	PORT_START("P2")
	PORT_BIT( 0x01, IP_ACTIVE_LOW, IPT_JOYSTICK_LEFT ) PORT_8WAY PORT_COCKTAIL
	PORT_BIT( 0x02, IP_ACTIVE_LOW, IPT_JOYSTICK_RIGHT ) PORT_8WAY PORT_COCKTAIL
	PORT_BIT( 0x04, IP_ACTIVE_LOW, IPT_JOYSTICK_UP ) PORT_8WAY PORT_COCKTAIL
	PORT_BIT( 0x08, IP_ACTIVE_LOW, IPT_JOYSTICK_DOWN ) PORT_8WAY PORT_COCKTAIL
	PORT_BIT( 0x10, IP_ACTIVE_LOW, IPT_BUTTON1 ) PORT_COCKTAIL
	PORT_BIT( 0x60, IP_ACTIVE_LOW, IPT_UNUSED )
	PORT_BIT( 0x80, IP_ACTIVE_HIGH, IPT_VBLANK )

	PORT_START("P1")
	PORT_BIT( 0x01, IP_ACTIVE_LOW, IPT_JOYSTICK_LEFT ) PORT_8WAY
	PORT_BIT( 0x02, IP_ACTIVE_LOW, IPT_JOYSTICK_RIGHT ) PORT_8WAY
	PORT_BIT( 0x04, IP_ACTIVE_LOW, IPT_JOYSTICK_DOWN ) PORT_8WAY
	PORT_BIT( 0x08, IP_ACTIVE_LOW, IPT_JOYSTICK_UP ) PORT_8WAY
	PORT_BIT( 0x10, IP_ACTIVE_LOW, IPT_BUTTON1 )
	PORT_BIT( 0x20, IP_ACTIVE_LOW, IPT_UNUSED )
	PORT_BIT( 0x40, IP_ACTIVE_LOW, IPT_START1 )
	PORT_BIT( 0x80, IP_ACTIVE_LOW, IPT_START2 )

	PORT_START("COINS")
	PORT_BIT( 0x01, IP_ACTIVE_LOW, IPT_COIN1 )
	PORT_BIT( 0x02, IP_ACTIVE_LOW, IPT_COIN2 )
	PORT_BIT( 0x04, IP_ACTIVE_LOW, IPT_SERVICE1 )
	PORT_BIT( 0xf8, IP_ACTIVE_LOW, IPT_UNUSED )
INPUT_PORTS_END

static INPUT_PORTS_START( zerotrgta )
	PORT_INCLUDE( zerotrgt )

	PORT_MODIFY("DSW0")
	PORT_DIPNAME( 0x03, 0x03, DEF_STR( Coin_A ) ) PORT_DIPLOCATION("SW1:1,2")
	PORT_DIPSETTING(    0x00, DEF_STR( 2C_1C ) )
	PORT_DIPSETTING(    0x03, DEF_STR( 1C_1C ) )
	PORT_DIPSETTING(    0x02, DEF_STR( 1C_2C ) )
	PORT_DIPSETTING(    0x01, DEF_STR( 1C_3C ) )
	PORT_DIPNAME( 0x0c, 0x0c, DEF_STR( Coin_B ) ) PORT_DIPLOCATION("SW1:3,4")
	PORT_DIPSETTING(    0x00, DEF_STR( 2C_1C ) )
	PORT_DIPSETTING(    0x0c, DEF_STR( 1C_1C ) )
	PORT_DIPSETTING(    0x08, DEF_STR( 1C_2C ) )
	PORT_DIPSETTING(    0x04, DEF_STR( 1C_3C ) )
INPUT_PORTS_END

static INPUT_CHANGED( coin_inserted )
{
	cntsteer_state *state = field->port->machine().driver_data<cntsteer_state>();
	device_set_input_line(state->m_subcpu, INPUT_LINE_NMI, newval ? CLEAR_LINE : ASSERT_LINE);
}

static INPUT_PORTS_START( cntsteer )
	PORT_START("P1")
	PORT_BIT( 0x0f, 0x00, IPT_PEDAL ) PORT_MINMAX(0x00,0x0f) PORT_SENSITIVITY(25) PORT_KEYDELTA(10) //todo
	PORT_BIT( 0x10, IP_ACTIVE_LOW, IPT_START1 )
	PORT_DIPNAME( 0x20, 0x20, DEF_STR( Unknown ) )
	PORT_DIPSETTING(    0x20, DEF_STR( Off ) )
	PORT_DIPSETTING(    0x00, DEF_STR( On ) )
	PORT_DIPNAME( 0x40, 0x40, DEF_STR( Unknown ) )
	PORT_DIPSETTING(    0x40, DEF_STR( Off ) )
	PORT_DIPSETTING(    0x00, DEF_STR( On ) )
	PORT_DIPNAME( 0x80, 0x80, DEF_STR( Unknown ) )
	PORT_DIPSETTING(    0x80, DEF_STR( Off ) )
	PORT_DIPSETTING(    0x00, DEF_STR( On ) )

	PORT_START("AN_STEERING")
	PORT_BIT( 0xff, 0x80, IPT_AD_STICK_X ) PORT_MINMAX(0x01,0xff) PORT_SENSITIVITY(10) PORT_KEYDELTA(2)

	PORT_START("COINS")
	PORT_BIT( 0x01, IP_ACTIVE_LOW, IPT_COIN1 ) PORT_IMPULSE(1) PORT_CHANGED(coin_inserted, 0)
	PORT_BIT( 0x02, IP_ACTIVE_LOW, IPT_COIN2 ) PORT_IMPULSE(1) PORT_CHANGED(coin_inserted, 0)
	PORT_BIT( 0x04, IP_ACTIVE_LOW, IPT_SERVICE1 ) PORT_IMPULSE(1) PORT_CHANGED(coin_inserted, 0)
	PORT_DIPNAME( 0x08, 0x08, DEF_STR( Unknown ) ) //unused
	PORT_DIPSETTING(    0x08, DEF_STR( Off ) )
	PORT_DIPSETTING(    0x00, DEF_STR( On ) )
	PORT_DIPNAME( 0x10, 0x10, DEF_STR( Unknown ) )
	PORT_DIPSETTING(    0x10, DEF_STR( Off ) )
	PORT_DIPSETTING(    0x00, DEF_STR( On ) )
	PORT_DIPNAME( 0x20, 0x20, DEF_STR( Unknown ) )
	PORT_DIPSETTING(    0x20, DEF_STR( Off ) )
	PORT_DIPSETTING(    0x00, DEF_STR( On ) )
	PORT_DIPNAME( 0x40, 0x40, DEF_STR( Unknown ) )
	PORT_DIPSETTING(    0x40, DEF_STR( Off ) )
	PORT_DIPSETTING(    0x00, DEF_STR( On ) )
	PORT_BIT( 0x80, IP_ACTIVE_HIGH, IPT_VBLANK )

	PORT_START("DSW0")
	PORT_DIPNAME( 0x03, 0x03, DEF_STR( Coin_A ) ) PORT_DIPLOCATION("SW1:1,2")
	PORT_DIPSETTING(    0x00, DEF_STR( 2C_1C ) )
	PORT_DIPSETTING(    0x03, DEF_STR( 1C_1C ) )
	PORT_DIPSETTING(    0x02, DEF_STR( 1C_2C ) )
	PORT_DIPSETTING(    0x01, DEF_STR( 1C_3C ) )
	PORT_DIPNAME( 0x0c, 0x0c, DEF_STR( Coin_B ) ) PORT_DIPLOCATION("SW1:3,4")
	PORT_DIPSETTING(    0x00, DEF_STR( 2C_1C ) )
	PORT_DIPSETTING(    0x0c, DEF_STR( 1C_1C ) )
	PORT_DIPSETTING(    0x08, DEF_STR( 1C_2C ) )
	PORT_DIPSETTING(    0x04, DEF_STR( 1C_3C ) )
	PORT_DIPNAME( 0x10, 0x10, DEF_STR( Service_Mode ) ) PORT_DIPLOCATION("SW1:5")
	PORT_DIPSETTING(    0x10, DEF_STR( Off ) )
	PORT_DIPSETTING(    0x00, DEF_STR( On ) )
	PORT_DIPNAME( 0x20, 0x20, DEF_STR( Unknown ) ) PORT_DIPLOCATION("SW1:6")
	PORT_DIPSETTING(    0x20, DEF_STR( Off ) )
	PORT_DIPSETTING(    0x00, DEF_STR( On ) )
	PORT_DIPNAME( 0x40, 0x40, DEF_STR( Difficulty ) ) PORT_DIPLOCATION("SW1:7")
	PORT_DIPSETTING(    0x40, DEF_STR( Normal ) )
	PORT_DIPSETTING(    0x00, DEF_STR( Hard ) )
	PORT_DIPNAME( 0x80, 0x80, DEF_STR( Unknown ) ) PORT_DIPLOCATION("SW1:8")
	PORT_DIPSETTING(    0x80, DEF_STR( Off ) )
	PORT_DIPSETTING(    0x00, DEF_STR( On ) )
INPUT_PORTS_END

/***************************************************************************/

static const gfx_layout cntsteer_charlayout =
{
	8,8,	/* 8*8 characters */
	0x200,
	2,	/* 2 bits per pixel */
	{ 0, 4 },	/* the two bitplanes for 4 pixels are packed into one byte */
	{ 0, 1, 2, 3, 0x800*8+0, 0x800*8+1, 0x800*8+2, 0x800*8+3 },
	{ 0*8, 1*8, 2*8, 3*8, 4*8, 5*8, 6*8, 7*8 },
	8*8	/* every char takes 8 consecutive bytes */
};

static const gfx_layout zerotrgt_charlayout =
{
	8,8,
	0x200,
	2,
	{ 0,4 },
	{ 0, 1, 2, 3, 1024*8*8+0, 1024*8*8+1, 1024*8*8+2, 1024*8*8+3 },
	{ 0*8, 1*8, 2*8, 3*8, 4*8, 5*8, 6*8, 7*8 },
	8*8	/* every tile takes 32 consecutive bytes */
};

static const gfx_layout sprites =
{
	16,16,
	RGN_FRAC(1,3),
	3,
	{ RGN_FRAC(0,3), RGN_FRAC(1,3), RGN_FRAC(2,3) },
	{ 16*8, 1+16*8, 2+16*8, 3+16*8, 4+16*8, 5+16*8, 6+16*8, 7+16*8,
		0, 1, 2, 3, 4, 5, 6, 7 },
	{ 0*8, 1*8, 2*8, 3*8, 4*8, 5*8, 6*8, 7*8 ,8*8,9*8,10*8,11*8,12*8,13*8,14*8,15*8 },
	16*16
};

static const gfx_layout tilelayout =
{
	16,16,
	0x400,
	3,	/* 3 bits per pixel */
	{ RGN_FRAC(4,8)+4, 0, 4 },
	{ 3, 2, 1, 0, 11, 10, 9 , 8, 19, 18, 17,16, 27, 26, 25, 24 },
	{
		RGN_FRAC(0,8)+0*8,	RGN_FRAC(1,8)+0*8, RGN_FRAC(2,8)+0*8, RGN_FRAC(3,8)+0*8,
		RGN_FRAC(0,8)+4*8,	RGN_FRAC(1,8)+4*8, RGN_FRAC(2,8)+4*8, RGN_FRAC(3,8)+4*8,
		RGN_FRAC(0,8)+8*8,	RGN_FRAC(1,8)+8*8, RGN_FRAC(2,8)+8*8, RGN_FRAC(3,8)+8*8,
		RGN_FRAC(0,8)+12*8, RGN_FRAC(1,8)+12*8,RGN_FRAC(2,8)+12*8,RGN_FRAC(3,8)+12*8
	},
	8*16
};

static GFXDECODE_START( cntsteer )
	GFXDECODE_ENTRY( "gfx1", 0x00000, cntsteer_charlayout, 0, 256 ) /* Only 1 used so far :/ */
	GFXDECODE_ENTRY( "gfx2", 0x00000, sprites,			  0, 256 )
	GFXDECODE_ENTRY( "gfx3", 0x00000, tilelayout,		  0, 256 )
GFXDECODE_END


static GFXDECODE_START( zerotrgt )
	GFXDECODE_ENTRY( "gfx1", 0x00000, zerotrgt_charlayout, 0, 256 ) /* Only 1 used so far :/ */
	GFXDECODE_ENTRY( "gfx2", 0x00000, sprites,			  0, 256 )
	GFXDECODE_ENTRY( "gfx3", 0x00000, tilelayout,		  0, 256 )
GFXDECODE_END

/***************************************************************************/

static MACHINE_START( cntsteer )
{
	cntsteer_state *state = machine.driver_data<cntsteer_state>();

	state->m_maincpu = machine.device("maincpu");
	state->m_audiocpu = machine.device("audiocpu");
	state->m_subcpu = machine.device("subcpu");

	state->save_item(NAME(state->m_flipscreen));
	state->save_item(NAME(state->m_bg_bank));
	state->save_item(NAME(state->m_scrolly));
	state->save_item(NAME(state->m_scrollx));
	state->save_item(NAME(state->m_scrollx_hi));
	state->save_item(NAME(state->m_scrolly_hi));
	state->save_item(NAME(state->m_rotation_x));
	state->save_item(NAME(state->m_rotation_sign));

	state->save_item(NAME(state->m_bg_color_bank));
	state->save_item(NAME(state->m_disable_roz));
}

static MACHINE_START( zerotrgt )
{
	cntsteer_state *state = machine.driver_data<cntsteer_state>();

	state->save_item(NAME(state->m_nmimask));
	MACHINE_START_CALL(cntsteer);
}


static MACHINE_RESET( cntsteer )
{
	cntsteer_state *state = machine.driver_data<cntsteer_state>();

	state->m_flipscreen = 0;
	state->m_bg_bank = 0;
	state->m_scrolly = 0;
	state->m_scrollx = 0;
	state->m_scrollx_hi = 0;
	state->m_scrolly_hi = 0;
	state->m_rotation_x = 0;
	state->m_rotation_sign = 0;

	state->m_bg_color_bank = 0;
	state->m_disable_roz = 0;
}


static MACHINE_RESET( zerotrgt )
{
	cntsteer_state *state = machine.driver_data<cntsteer_state>();

	state->m_nmimask = 0;
	MACHINE_RESET_CALL(cntsteer);
}

static const ay8910_interface ay8910_config =
{
	AY8910_LEGACY_OUTPUT,
	AY8910_DEFAULT_LOADS,
	DEVCB_NULL,
	DEVCB_NULL,
	DEVCB_DEVICE_HANDLER("dac", dac_w),
	DEVCB_NULL
};

static MACHINE_CONFIG_START( cntsteer, cntsteer_state )

	/* basic machine hardware */
	MCFG_CPU_ADD("maincpu", M6809, 2000000)		 /* ? */
	MCFG_CPU_PROGRAM_MAP(cntsteer_cpu1_map)
	MCFG_CPU_VBLANK_INT("screen", nmi_line_pulse) /* ? */

	MCFG_CPU_ADD("subcpu", M6809, 2000000)		 /* ? */
	MCFG_CPU_PROGRAM_MAP(cntsteer_cpu2_map)
//  MCFG_CPU_VBLANK_INT("screen", nmi_line_pulse) /* ? */

	MCFG_CPU_ADD("audiocpu", M6502, 1500000)        /* ? */
	MCFG_CPU_PROGRAM_MAP(sound_map)
	MCFG_CPU_PERIODIC_INT(sound_interrupt, 480)

	MCFG_MACHINE_START(cntsteer)
	MCFG_MACHINE_RESET(cntsteer)

	/* video hardware */
	MCFG_SCREEN_ADD("screen", RASTER)
	MCFG_SCREEN_REFRESH_RATE(60)
	MCFG_SCREEN_VBLANK_TIME(ATTOSECONDS_IN_USEC(2500) /* not accurate */)
	MCFG_SCREEN_FORMAT(BITMAP_FORMAT_INDEXED16)
	MCFG_SCREEN_SIZE(256, 256)
	MCFG_SCREEN_VISIBLE_AREA(0*8, 32*8-1, 1*8, 31*8-1)
	MCFG_SCREEN_UPDATE(cntsteer)

	MCFG_QUANTUM_TIME(attotime::from_hz(6000))

	MCFG_GFXDECODE(cntsteer)
	MCFG_PALETTE_LENGTH(256)
//  MCFG_PALETTE_INIT(zerotrgt)

	MCFG_VIDEO_START(cntsteer)

	/* sound hardware */
	MCFG_SPEAKER_STANDARD_MONO("mono")
	MCFG_SOUND_ADD("ay1", AY8910, 1500000)
	MCFG_SOUND_CONFIG(ay8910_config)
	MCFG_SOUND_ROUTE(ALL_OUTPUTS, "mono", 0.50)

	MCFG_SOUND_ADD("ay2", AY8910, 1500000)
	MCFG_SOUND_ROUTE(ALL_OUTPUTS, "mono", 0.50)

	MCFG_SOUND_ADD("dac", DAC, 0)
	MCFG_SOUND_ROUTE(ALL_OUTPUTS, "mono", 0.50)
MACHINE_CONFIG_END

static MACHINE_CONFIG_START( zerotrgt, cntsteer_state )

	/* basic machine hardware */
	MCFG_CPU_ADD("maincpu", M6809, 2000000)		 /* ? */
	MCFG_CPU_PROGRAM_MAP(gekitsui_cpu1_map)
	MCFG_CPU_VBLANK_INT("screen", nmi_line_pulse) /* ? */

	MCFG_CPU_ADD("subcpu", M6809, 2000000)		 /* ? */
	MCFG_CPU_PROGRAM_MAP(gekitsui_cpu2_map)
//  MCFG_CPU_VBLANK_INT("screen", nmi_line_pulse) /* ? */

	MCFG_CPU_ADD("audiocpu", M6502, 1500000)		/* ? */
	MCFG_CPU_PROGRAM_MAP(sound_map)
	MCFG_CPU_PERIODIC_INT(sound_interrupt, 480)

	MCFG_QUANTUM_TIME(attotime::from_hz(6000))

	MCFG_MACHINE_START(zerotrgt)
	MCFG_MACHINE_RESET(zerotrgt)

	/* video hardware */
	MCFG_SCREEN_ADD("screen", RASTER)
	MCFG_SCREEN_REFRESH_RATE(60)
	MCFG_SCREEN_VBLANK_TIME(ATTOSECONDS_IN_USEC(2500) /* not accurate */)
	MCFG_SCREEN_FORMAT(BITMAP_FORMAT_INDEXED16)
	MCFG_SCREEN_SIZE(256, 256)
	MCFG_SCREEN_VISIBLE_AREA(0*8, 32*8-1, 1*8, 31*8-1)
	MCFG_SCREEN_UPDATE(zerotrgt)

	MCFG_GFXDECODE(zerotrgt)
	MCFG_PALETTE_LENGTH(256)

	MCFG_PALETTE_INIT(zerotrgt)
	MCFG_VIDEO_START(zerotrgt)

	/* sound hardware */
	MCFG_SPEAKER_STANDARD_MONO("mono")

	MCFG_SOUND_ADD("ay1", AY8910, 1500000)
	MCFG_SOUND_ROUTE(ALL_OUTPUTS, "mono", 0.50)

	MCFG_SOUND_ADD("ay2", AY8910, 1500000)
	MCFG_SOUND_ROUTE(ALL_OUTPUTS, "mono", 0.50)
MACHINE_CONFIG_END

/***************************************************************************/

ROM_START( cntsteer )
	ROM_REGION( 0x10000, "maincpu", 0 )
	ROM_LOAD( "by02", 0x8000, 0x4000, CRC(b6fdd7fd) SHA1(e54cc31628966f747f9ccbf9db1017ed1eee0d5d) )
	ROM_LOAD( "by01", 0xc000, 0x4000, CRC(932423a5) SHA1(0d8164359a79ae554328dfb4d729a8d07de7ee75) )

	ROM_REGION( 0x10000, "subcpu", 0 )
	ROM_LOAD( "by12", 0x4000, 0x4000, CRC(278e7fed) SHA1(5def4c8919a507c64045c57de2da65e1d39e1185) )
	ROM_LOAD( "by11", 0x8000, 0x4000, CRC(00624e34) SHA1(27bd472e9f2feef4a2c4753d8b0da26ff30d930d) )
	ROM_LOAD( "by10", 0xc000, 0x4000, CRC(9227a9ce) SHA1(8c86f22f90a3a8853562469037ffa06693045f4c) )

	ROM_REGION( 0x10000, "audiocpu", 0 )
	ROM_LOAD( "by00", 0xe000, 0x2000, CRC(740e4896) SHA1(959652515188966e1c2810eabf2f428fe31a31a9) )

	ROM_REGION( 0x4000, "gfx1", 0 ) /* Characters */
	ROM_LOAD( "by09", 0x0000, 0x2000, CRC(273eddae) SHA1(4b5450407217d9110acb85e02ea9a6584552362e) )

	ROM_REGION( 0x18000, "gfx2", 0 ) /* Sprites */
	ROM_LOAD( "by03", 0x00000, 0x4000, CRC(d9537d33) SHA1(7d2af2eb0386ce695f2d9c7b71a72d2d8ef257e7) )
	ROM_LOAD( "by04", 0x04000, 0x4000, CRC(4f4e9d6f) SHA1(b590aeb5efa2afa50ef202191a88bcf6894f4b8e) )
	ROM_LOAD( "by05", 0x08000, 0x4000, CRC(592481a7) SHA1(2d412d525b04ed228a345918129b25a13286d957) )
	ROM_LOAD( "by06", 0x0c000, 0x4000, CRC(9366e9d5) SHA1(a6a137416eaee3becae657c287fff7d974bcf68f) )
	ROM_LOAD( "by07", 0x10000, 0x4000, CRC(8321e332) SHA1(a7aed12cb718526b0a1c5b4ae069c7973600204d) )
	ROM_LOAD( "by08", 0x14000, 0x4000, CRC(a24bcfef) SHA1(b4f06dfb85960668ca199cfb1b6c56ccdad9e33d) )

	ROM_REGION( 0x80000, "gfx3", 0 ) /* Tiles */
	ROM_LOAD( "by13", 0x00000, 0x4000, CRC(d38e94fd) SHA1(bcf61b2c509f923ef2e52051a1c0e0a63bedf7a3) )
	ROM_LOAD( "by15", 0x10000, 0x4000, CRC(b0c9de83) SHA1(b0041273fe968667a09c243d393b2b025c456c99) )
	ROM_LOAD( "by17", 0x20000, 0x4000, CRC(8aff285f) SHA1(d40332448e7fb20389ac18661569726f229bd9d6) )
	ROM_LOAD( "by19", 0x30000, 0x4000, CRC(7eff6d02) SHA1(967ab34bb969228689541c0a2eabd3e96665676d) )
	/* roms from "gfx4" are expanded here */

	ROM_REGION( 0x40000, "gfx4", 0 ) /* Tiles */
	ROM_LOAD( "by14", 0x00000, 0x2000, CRC(4db6c146) SHA1(93d157f4c4ffa2d7b4c0b33fedabd6d750245033) )
	ROM_LOAD( "by16", 0x10000, 0x2000, CRC(adede1e6) SHA1(87e0323b6d2f2d8a3585cd78c9dc9d384106b005) )
	ROM_LOAD( "by18", 0x20000, 0x2000, CRC(1e9ce047) SHA1(7579ba6b401eb1bfc7d2d9311ebab623bd1095a2) )
	ROM_LOAD( "by20", 0x30000, 0x2000, CRC(e2198c9e) SHA1(afea262db9154301f4b9e53e1fc91985dd934170) )

	ROM_REGION( 0x300, "proms", ROMREGION_ERASE00 )
	ROM_LOAD( "by21.j4",  0x0000, 0x100, NO_DUMP ) /* All 82s129 or equivalent */
	ROM_LOAD( "by22.j5",  0x0100, 0x100, NO_DUMP )
	ROM_LOAD( "by23.j6",  0x0200, 0x100, NO_DUMP )
ROM_END

ROM_START( zerotrgt )
	ROM_REGION( 0x10000, "maincpu", 0 )
	ROM_LOAD( "cw01.c4", 0x8000, 0x8000, CRC(b35a16cb) SHA1(49581324c3e3d5219f0512d08a40161185368b10) )

	ROM_REGION( 0x10000, "subcpu", 0 )
	ROM_LOAD( "cw08.a16",  0x4000, 0x4000,  CRC(c485a494) SHA1(9b44494f88ef555b09ad3f587c91584911b2a3ab) )
	ROM_LOAD( "cw19.a13",  0x8000, 0x8000,  CRC(e95b9b34) SHA1(d0bcf39df1fab0affd307102616f13975606194f) )

	ROM_REGION( 0x10000, "audiocpu", 0 )
	ROM_LOAD( "cw00.c1",  0xe000, 0x2000,  CRC(ae091b6c) SHA1(8b3a1c0acbfa56f05bcf65677f85d70c8c9640d6) )

	ROM_REGION( 0x04000, "gfx1", 0 ) /* Characters */
	ROM_LOAD( "cw05.h16", 0x00000, 0x4000, CRC(e7a24404) SHA1(a8a33118d4f09b77cfd7e6e9f486b250078b21bc) )

	ROM_REGION( 0x18000, "gfx2", 0 ) /* Sprites */
	ROM_LOAD( "cw02.c14", 0x00000, 0x8000, CRC(d2a0bb72) SHA1(ee060f8db0b1fa1ba1034bf94cf44ff6820660bd) )
	ROM_LOAD( "cw03.c15", 0x08000, 0x8000, CRC(79f2be20) SHA1(62cf55d9163d522b7cb0e760f0d5662c529a22e9) )
	ROM_LOAD( "cw04.c17", 0x10000, 0x8000, CRC(1037cce8) SHA1(11e49e29f9b60fbf36a301a566f233eb6150d519) )

	ROM_REGION( 0x80000, "gfx3", 0 ) /* Tiles */
	ROM_LOAD( "cw09.j4",  0x00000, 0x4000, CRC(8c859d41) SHA1(8095e83de81d2c9f270a303322ddf84568e3d37a) )
	ROM_LOAD( "cw11.j7",  0x10000, 0x4000, CRC(5da2d9d8) SHA1(d2cfdbf892bce3667545568998aa03bfd03155c5) )
	ROM_LOAD( "cw13.j10", 0x20000, 0x4000, CRC(b004cedd) SHA1(2a503ea14c66805b37f25096ecfec19a07cdc387) )
	ROM_LOAD( "cw15.j13", 0x30000, 0x4000, CRC(4473fe66) SHA1(0accbcb801f58df410af305a87a960e526f8a25a) )
	/* roms from "gfx4" are expanded here */

	ROM_REGION( 0x40000, "gfx4", 0 ) /* Tiles */
	ROM_LOAD( "cw10.j6",  0x00000, 0x2000, CRC(16073975) SHA1(124128db649116d675503b03310ebbd919d5a837) )
	ROM_LOAD( "cw12.j9",  0x10000, 0x2000, CRC(9776974e) SHA1(7e944379c3ff3211c84bd4b48cebbd52c586ff88) )
	ROM_LOAD( "cw14.j12", 0x20000, 0x2000, CRC(5f77e84d) SHA1(ef7a53ad40ef5d3b7ceecb174099b8f2adfda92e) )
	ROM_LOAD( "cw16.j15", 0x30000, 0x2000, CRC(ebed04d3) SHA1(df5484ab44ddf91fddbb895606875b6733b03a51) )

	ROM_REGION( 0x200, "proms", 0 )
	ROM_LOAD( "mb7118h.k7",  0x0000, 0x100, CRC(4a7c187a) SHA1(2463ed582b77252a798b946cc831c4edd6e6b31f) )
	ROM_LOAD( "6301-in.k6",   0x0100, 0x100, CRC(cc9c7d43) SHA1(707fcc9579bae4233903142efa7dfee7d463ae9a) )

	ROM_REGION( 0x0300, "plds", 0 )
	ROM_LOAD( "pal10h8.f12", 0x0000, 0x002c, NO_DUMP )
	ROM_LOAD( "pal10h8.e14", 0x0100, 0x002c, NO_DUMP ) /* PAL is read protected */
	ROM_LOAD( "pal12l6.f17", 0x0200, 0x0034, NO_DUMP )
ROM_END

ROM_START( zerotrgta )
	ROM_REGION( 0x10000, "maincpu", 0 )
	ROM_LOAD( "ct01-s.4c", 0x8000, 0x8000, CRC(b35a16cb) SHA1(49581324c3e3d5219f0512d08a40161185368b10) )

	ROM_REGION( 0x10000, "subcpu", 0 )
	ROM_LOAD( "ct08.16a",  0x4000, 0x4000,  CRC(7e8db408) SHA1(2ae407d15645753a2a0d691c9f1cf1eb383d3e8a) )
	ROM_LOAD( "cty07.14a", 0x8000, 0x4000,  CRC(119b6211) SHA1(2042f06387d34fad6b63bcb8ac6f9b06377f634d) )
	ROM_LOAD( "ct06.13a",  0xc000, 0x4000,  CRC(bce5adad) SHA1(86c4eef0d68679a24bab6460b49640a498f32ecd) )

	ROM_REGION( 0x10000, "audiocpu", 0 )
	ROM_LOAD( "ct00.1c",  0xe000, 0x2000,  CRC(ae091b6c) SHA1(8b3a1c0acbfa56f05bcf65677f85d70c8c9640d6) )

	ROM_REGION( 0x04000, "gfx1", 0 ) /* Characters */
	ROM_LOAD( "ct05.16h", 0x00000, 0x4000, CRC(e7a24404) SHA1(a8a33118d4f09b77cfd7e6e9f486b250078b21bc) )

	ROM_REGION( 0x18000, "gfx2", 0 ) /* Sprites */
	ROM_LOAD( "ct02.14c", 0x00000, 0x8000, CRC(d2a0bb72) SHA1(ee060f8db0b1fa1ba1034bf94cf44ff6820660bd) )
	ROM_LOAD( "ct03.15c", 0x08000, 0x8000, CRC(79f2be20) SHA1(62cf55d9163d522b7cb0e760f0d5662c529a22e9) )
	ROM_LOAD( "ct04.17c", 0x10000, 0x8000, CRC(1037cce8) SHA1(11e49e29f9b60fbf36a301a566f233eb6150d519) )

	ROM_REGION( 0x80000, "gfx3", 0 ) /* Tiles */
	ROM_LOAD( "ct09.4j",  0x00000, 0x4000, CRC(8c859d41) SHA1(8095e83de81d2c9f270a303322ddf84568e3d37a) )
	ROM_LOAD( "ct11.7j",  0x10000, 0x4000, CRC(5da2d9d8) SHA1(d2cfdbf892bce3667545568998aa03bfd03155c5) )
	ROM_LOAD( "ct13.10j", 0x20000, 0x4000, CRC(b004cedd) SHA1(2a503ea14c66805b37f25096ecfec19a07cdc387) )
	ROM_LOAD( "ct15.13j", 0x30000, 0x4000, CRC(4473fe66) SHA1(0accbcb801f58df410af305a87a960e526f8a25a) )
	/* roms from "gfx4" are expanded here */

	ROM_REGION( 0x40000, "gfx4", 0 ) /* Tiles */
	ROM_LOAD( "ct10.6j",  0x00000, 0x2000, CRC(16073975) SHA1(124128db649116d675503b03310ebbd919d5a837) )
	ROM_LOAD( "ct12.9j",  0x10000, 0x2000, CRC(9776974e) SHA1(7e944379c3ff3211c84bd4b48cebbd52c586ff88) )
	ROM_LOAD( "ct14.12j", 0x20000, 0x2000, CRC(5f77e84d) SHA1(ef7a53ad40ef5d3b7ceecb174099b8f2adfda92e) )
	ROM_LOAD( "ct16.15j", 0x30000, 0x2000, CRC(ebed04d3) SHA1(df5484ab44ddf91fddbb895606875b6733b03a51) )

	ROM_REGION( 0x200, "proms", 0 )
	ROM_LOAD( "mb7118h.7k",  0x0000, 0x100, CRC(4a7c187a) SHA1(2463ed582b77252a798b946cc831c4edd6e6b31f) )
	ROM_LOAD( "mb7052.6k",   0x0100, 0x100, CRC(cc9c7d43) SHA1(707fcc9579bae4233903142efa7dfee7d463ae9a) )

	ROM_REGION( 0x0300, "plds", 0 )
	ROM_LOAD( "pal10h8.12f", 0x0000, 0x002c, CRC(173f9798) SHA1(8b0b0314d25a70e098df5d93191669738d3e57af) )
	ROM_LOAD( "pal10h8.14e", 0x0100, 0x002c, NO_DUMP ) /* PAL is read protected */
	ROM_LOAD( "pal12l6.17f", 0x0200, 0x0034, CRC(29b7e869) SHA1(85bdb6872d148c393c4cd98872b4920444394620) )
ROM_END

ROM_START( gekitsui )
	ROM_REGION( 0x10000, "maincpu", 0 )
	ROM_LOAD( "ct01", 0x8000, 0x8000, CRC(d3d82d8d) SHA1(c175c626d4cb89a2d82740c04892092db6faf616) )

	ROM_REGION( 0x10000, "subcpu", 0 )
	ROM_LOAD( "ct08.16a",  0x4000, 0x4000,  CRC(7e8db408) SHA1(2ae407d15645753a2a0d691c9f1cf1eb383d3e8a) )
	ROM_LOAD( "cty07.14a", 0x8000, 0x4000,  CRC(119b6211) SHA1(2042f06387d34fad6b63bcb8ac6f9b06377f634d) )
	ROM_LOAD( "ct06.13a",  0xc000, 0x4000,  CRC(bce5adad) SHA1(86c4eef0d68679a24bab6460b49640a498f32ecd) )

	ROM_REGION( 0x10000, "audiocpu", 0 )
	ROM_LOAD( "ct00.1c",  0xe000, 0x2000,  CRC(ae091b6c) SHA1(8b3a1c0acbfa56f05bcf65677f85d70c8c9640d6) )

	ROM_REGION( 0x04000, "gfx1", 0 )
	ROM_LOAD( "ct05", 0x00000, 0x4000, CRC(b9e997a1) SHA1(5891cb0984bf4a1ccd80ef338c47e3d5705a1331) )	/* Characters */

	ROM_REGION( 0x18000, "gfx2", 0 ) /* Sprites */
	ROM_LOAD( "ct02.14c", 0x00000, 0x8000, CRC(d2a0bb72) SHA1(ee060f8db0b1fa1ba1034bf94cf44ff6820660bd) )
	ROM_LOAD( "ct03.15c", 0x08000, 0x8000, CRC(79f2be20) SHA1(62cf55d9163d522b7cb0e760f0d5662c529a22e9) )
	ROM_LOAD( "ct04.17c", 0x10000, 0x8000, CRC(1037cce8) SHA1(11e49e29f9b60fbf36a301a566f233eb6150d519) )

	ROM_REGION( 0x80000, "gfx3", 0 ) /* Tiles */
	ROM_LOAD( "ct09.4j",  0x00000, 0x4000, CRC(8c859d41) SHA1(8095e83de81d2c9f270a303322ddf84568e3d37a) )
	ROM_LOAD( "ct11.7j",  0x10000, 0x4000, CRC(5da2d9d8) SHA1(d2cfdbf892bce3667545568998aa03bfd03155c5) )
	ROM_LOAD( "ct13.10j", 0x20000, 0x4000, CRC(b004cedd) SHA1(2a503ea14c66805b37f25096ecfec19a07cdc387) )
	ROM_LOAD( "ct15.13j", 0x30000, 0x4000, CRC(4473fe66) SHA1(0accbcb801f58df410af305a87a960e526f8a25a) )
	/* roms from "gfx4" are expanded here */

	ROM_REGION( 0x40000, "gfx4", 0 ) /* Tiles */
	ROM_LOAD( "ct10.6j",  0x00000, 0x2000, CRC(16073975) SHA1(124128db649116d675503b03310ebbd919d5a837) )
	ROM_LOAD( "ct12.9j",  0x10000, 0x2000, CRC(9776974e) SHA1(7e944379c3ff3211c84bd4b48cebbd52c586ff88) )
	ROM_LOAD( "ct14.12j", 0x20000, 0x2000, CRC(5f77e84d) SHA1(ef7a53ad40ef5d3b7ceecb174099b8f2adfda92e) )
	ROM_LOAD( "ct16.15j", 0x30000, 0x2000, CRC(ebed04d3) SHA1(df5484ab44ddf91fddbb895606875b6733b03a51) )

	ROM_REGION( 0x200, "proms", 0 )
	ROM_LOAD( "mb7118h.7k",  0x0000, 0x100, CRC(4a7c187a) SHA1(2463ed582b77252a798b946cc831c4edd6e6b31f) )
	ROM_LOAD( "mb7052.6k",   0x0100, 0x100, CRC(cc9c7d43) SHA1(707fcc9579bae4233903142efa7dfee7d463ae9a) )

	ROM_REGION( 0x0300, "plds", 0 )
	ROM_LOAD( "pal10h8.12f", 0x0000, 0x002c, CRC(173f9798) SHA1(8b0b0314d25a70e098df5d93191669738d3e57af) )
	ROM_LOAD( "pal10h8.14e", 0x0100, 0x002c, NO_DUMP ) /* PAL is read protected */
	ROM_LOAD( "pal12l6.17f", 0x0200, 0x0034, CRC(29b7e869) SHA1(85bdb6872d148c393c4cd98872b4920444394620) )
ROM_END

/***************************************************************************/

static void zerotrgt_rearrange_gfx( running_machine &machine, int romsize, int romarea )
{
	UINT8 *src = machine.region("gfx4")->base();
	UINT8 *dst = machine.region("gfx3")->base();
	int rm;
	int cnt1;

	dst += romarea * 4;

	for (rm = 0; rm < 4; rm++)
	{
		for (cnt1 = 0; cnt1 < romsize; cnt1++)
		{
			dst[rm * romarea + cnt1] = (src[rm * romarea + cnt1] & 0x0f);
			dst[rm * romarea + cnt1 + romsize] = (src[rm * romarea + cnt1] & 0xf0) >> 4;
		}
	}
}

#if 0
static DRIVER_INIT( cntsteer )
{
	UINT8 *RAM = machine.region("subcpu")->base();

	RAM[0xc2cf] = 0x43; /* Patch out Cpu 1 ram test - it never ends..?! */
	RAM[0xc2d0] = 0x43;
	RAM[0xc2f1] = 0x43;
	RAM[0xc2f2] = 0x43;

	zerotrgt_rearrange_gfx(machine, 0x02000, 0x10000);
}
#endif

static DRIVER_INIT( zerotrgt )
{
	zerotrgt_rearrange_gfx(machine, 0x02000, 0x10000);
}


/***************************************************************************/

GAME( 1985, zerotrgt,  0,        zerotrgt,  zerotrgt,  zerotrgt, ROT0,   "Data East Corporation", "Zero Target (World, CW)", GAME_IMPERFECT_GRAPHICS|GAME_IMPERFECT_SOUND|GAME_NO_COCKTAIL|GAME_NOT_WORKING|GAME_SUPPORTS_SAVE )
GAME( 1985, zerotrgta, zerotrgt, zerotrgt,  zerotrgta, zerotrgt, ROT0,   "Data East Corporation", "Zero Target (World, CT)", GAME_IMPERFECT_GRAPHICS|GAME_IMPERFECT_SOUND|GAME_NO_COCKTAIL|GAME_NOT_WORKING|GAME_SUPPORTS_SAVE )
GAME( 1985, gekitsui,  zerotrgt, zerotrgt,  zerotrgta, zerotrgt, ROT0,   "Data East Corporation", "Gekitsui Oh (Japan)", GAME_IMPERFECT_GRAPHICS|GAME_IMPERFECT_SOUND|GAME_NO_COCKTAIL|GAME_NOT_WORKING|GAME_SUPPORTS_SAVE )
GAME( 1985, cntsteer,  0,        cntsteer,  cntsteer,  zerotrgt, ROT270, "Data East Corporation", "Counter Steer (Japan)", GAME_IMPERFECT_GRAPHICS|GAME_IMPERFECT_SOUND|GAME_WRONG_COLORS|GAME_NO_COCKTAIL|GAME_NOT_WORKING|GAME_SUPPORTS_SAVE )
