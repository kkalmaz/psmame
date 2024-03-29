/***************************************************************************

        Poly-88 video by Miodrag Milanovic

        18/05/2009 Initial implementation

****************************************************************************/

#include "emu.h"
#include "includes/poly88.h"

static const UINT8 mcm6571a_shift[] =
{
	0,1,1,0,0,0,1,0,0,0,0,1,0,0,0,0,
	1,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,
	0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,1,0,0,1,0,0,0,0,0,
	1,1,0,0,0,0,0,0,0,1,0,0,0,0,0,0
};

VIDEO_START( poly88 )
{
	poly88_state *state = machine.driver_data<poly88_state>();
	state->m_FNT = machine.region("chargen")->base();
}

SCREEN_UPDATE( poly88 )
{
	poly88_state *state = screen->machine().driver_data<poly88_state>();
	int x,y,j,b;
	UINT16 addr;
	int xpos;
	UINT8 l,r;

	for(y = 0; y < 16; y++ )
	{
		addr = y*64;
		xpos = 0;
		for(x = 0; x < 64; x++ )
		{
			UINT8 code = state->m_video_ram[addr + x];
			if ((code & 0x80)==0)
			{
				for(j = 0; j < 15; j++ )
				{
					l = j/5;
					for(b = 0; b < 10; b++ )
					{
						r = b/5;
						if (l==0 && r==0)
							*BITMAP_ADDR16(bitmap, y*15+j, xpos+b ) = BIT(code,5) ? 0 : 1;

						if (l==0 && r==1)
							*BITMAP_ADDR16(bitmap, y*15+j, xpos+b ) = BIT(code,2) ? 0 : 1;

						if (l==1 && r==0)
							*BITMAP_ADDR16(bitmap, y*15+j, xpos+b ) = BIT(code,4) ? 0 : 1;

						if (l==1 && r==1)
							*BITMAP_ADDR16(bitmap, y*15+j, xpos+b ) = BIT(code,1) ? 0 : 1;

						if (l==2 && r==0)
							*BITMAP_ADDR16(bitmap, y*15+j, xpos+b ) = BIT(code,3) ? 0 : 1;

						if (l==2 && r==1)
							*BITMAP_ADDR16(bitmap, y*15+j, xpos+b ) = BIT(code,0) ? 0 : 1;
					}
				}
			}
			else
			{
				for(j = 0; j < 15; j++ )
				{
					code &= 0x7f;
					l = 0;
					if (mcm6571a_shift[code]==0)
					{
						if (j < 9)
							l = state->m_FNT[code*16 + j];
					}
					else
					{
						if ((j > 2) && (j < 12))
							l = state->m_FNT[code*16 + j - 3];
					}

					for(b = 0; b < 7; b++ )
						*BITMAP_ADDR16(bitmap, y*15+j, xpos+b ) =  (l >> (6-b)) & 1;

				  	*BITMAP_ADDR16(bitmap, y*15+j, xpos+7 ) =  0;
				  	*BITMAP_ADDR16(bitmap, y*15+j, xpos+8 ) =  0;
				  	*BITMAP_ADDR16(bitmap, y*15+j, xpos+9 ) =  0;
				}
			}
			xpos += 10;
		}
	}
	return 0;
}

