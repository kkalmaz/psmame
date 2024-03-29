/***************************************************************************

    Neo-Geo AES hardware

    Credits (from MAME neogeo.c, since this is just a minor edit of that driver):
        * This driver was made possible by the research done by
          Charles MacDonald.  For a detailed description of the Neo-Geo
          hardware, please visit his page at:
          http://cgfm2.emuviews.com/temp/mvstech.txt
        * Presented to you by the Shin Emu Keikaku team.
        * The following people have all spent probably far
          too much time on this:
          AVDB
          Bryan McPhail
          Fuzz
          Ernesto Corvi
          Andrew Prime
          Zsolt Vasvari

    MESS cartridge support by R. Belmont based on work by Michael Zapf

    Current status:
        - Cartridges run.

    ToDo :
        - Change input code to allow selection of the mahjong panel in PORT_CATEGORY.
        - Clean up code, to reduce duplication of MAME source

****************************************************************************/

#include "emu.h"
#include "cpu/m68000/m68000.h"
#include "includes/neogeo.h"
#include "machine/pd4990a.h"
#include "cpu/z80/z80.h"
#include "sound/2610intf.h"
#include "devices/aescart.h"
#include "imagedev/cartslot.h"
#ifdef MESS
#include "neogeo.lh"
#else
extern const char layout_neogeo[];
#endif


// CD-ROM / DMA control registers
typedef struct
{
	UINT8 area_sel;
	UINT8 pcm_bank_sel;
	UINT8 spr_bank_sel;
	UINT32 addr_source; // target if in fill mode
	UINT32 addr_target;
	UINT16 fill_word;
	UINT32 word_count;
	UINT16 dma_mode[10];
} neocd_ctrl_t;

class ng_aes_state : public neogeo_state
{
public:
	ng_aes_state(running_machine &machine, const driver_device_config_base &config)
		: neogeo_state(machine, config) { }

	UINT8 *m_memcard_data;
	neocd_ctrl_t m_neocd_ctrl;
};



#define LOG_VIDEO_SYSTEM		(0)
#define LOG_CPU_COMM			(0)
#define LOG_MAIN_CPU_BANKING	(1)
#define LOG_AUDIO_CPU_BANKING	(1)

#define NEOCD_AREA_SPR    0
#define NEOCD_AREA_PCM    1
#define NEOCD_AREA_AUDIO  4
#define NEOCD_AREA_FIX    5

/*************************************
 *
 *  Global variables
 *
 *************************************/

//static UINT16 *save_ram;

UINT16* neocd_work_ram;

/*************************************
 *
 *  Forward declarations
 *
 *************************************/

//static void set_output_latch(running_machine &machine, UINT8 data);
//static void set_output_data(running_machine &machine, UINT8 data);


/*************************************
 *
 *  Main CPU interrupt generation
 *
 *************************************/

#define IRQ2CTRL_ENABLE				(0x10)
#define IRQ2CTRL_LOAD_RELATIVE		(0x20)
#define IRQ2CTRL_AUTOLOAD_VBLANK	(0x40)
#define IRQ2CTRL_AUTOLOAD_REPEAT	(0x80)


static void adjust_display_position_interrupt_timer( running_machine &machine )
{
	neogeo_state *state = machine.driver_data<neogeo_state>();

	if ((state->m_display_counter + 1) != 0)
	{
		attotime period = (attotime::from_hz(NEOGEO_PIXEL_CLOCK) * (state->m_display_counter + 1));
		if (LOG_VIDEO_SYSTEM) logerror("adjust_display_position_interrupt_timer  current y: %02x  current x: %02x   target y: %x  target x: %x\n", machine.primary_screen->vpos(), machine.primary_screen->hpos(), (state->m_display_counter + 1) / NEOGEO_HTOTAL, (state->m_display_counter + 1) % NEOGEO_HTOTAL);

		state->m_display_position_interrupt_timer->adjust(period);
	}
}

#ifdef MESS
void neogeo_set_display_position_interrupt_control( running_machine &machine, UINT16 data )
{
	neogeo_state *state = machine.driver_data<neogeo_state>();
	state->m_display_position_interrupt_control = data;
}


void neogeo_set_display_counter_msb( address_space *space, UINT16 data )
{
	neogeo_state *state = space->machine().driver_data<neogeo_state>();

	state->m_display_counter = (state->m_display_counter & 0x0000ffff) | ((UINT32)data << 16);

	if (LOG_VIDEO_SYSTEM) logerror("PC %06x: set_display_counter %08x\n", cpu_get_pc(&space->device()), state->m_display_counter);
}


void neogeo_set_display_counter_lsb( address_space *space, UINT16 data )
{
	neogeo_state *state = space->machine().driver_data<neogeo_state>();

	state->m_display_counter = (state->m_display_counter & 0xffff0000) | data;

	if (LOG_VIDEO_SYSTEM) logerror("PC %06x: set_display_counter %08x\n", cpu_get_pc(&space->device()), state->m_display_counter);

	if (state->m_display_position_interrupt_control & IRQ2CTRL_LOAD_RELATIVE)
	{
		if (LOG_VIDEO_SYSTEM) logerror("AUTOLOAD_RELATIVE ");
		adjust_display_position_interrupt_timer(space->machine());
	}
}
#endif

static void update_interrupts( running_machine &machine )
{
	neogeo_state *state = machine.driver_data<neogeo_state>();

	if(strcmp((char*)machine.system().name,"aes") != 0)
	{  // raster and vblank IRQs are swapped on the NeoCD.
		cputag_set_input_line(machine, "maincpu", 2, state->m_vblank_interrupt_pending ? ASSERT_LINE : CLEAR_LINE);
		cputag_set_input_line(machine, "maincpu", 1, state->m_display_position_interrupt_pending ? ASSERT_LINE : CLEAR_LINE);
		cputag_set_input_line(machine, "maincpu", 3, state->m_irq3_pending ? ASSERT_LINE : CLEAR_LINE);
	}
	else
	{
		cputag_set_input_line(machine, "maincpu", 1, state->m_vblank_interrupt_pending ? ASSERT_LINE : CLEAR_LINE);
		cputag_set_input_line(machine, "maincpu", 2, state->m_display_position_interrupt_pending ? ASSERT_LINE : CLEAR_LINE);
		cputag_set_input_line(machine, "maincpu", 3, state->m_irq3_pending ? ASSERT_LINE : CLEAR_LINE);
	}
}

#ifdef MESS
void neogeo_acknowledge_interrupt( running_machine &machine, UINT16 data )
{
	neogeo_state *state = machine.driver_data<neogeo_state>();

	if (data & 0x01)
		state->m_irq3_pending = 0;
	if (data & 0x02)
		state->m_display_position_interrupt_pending = 0;
	if (data & 0x04)
		state->m_vblank_interrupt_pending = 0;

	update_interrupts(machine);
}
#endif

static TIMER_CALLBACK( display_position_interrupt_callback )
{
	neogeo_state *state = machine.driver_data<neogeo_state>();

	if (LOG_VIDEO_SYSTEM) logerror("--- Scanline @ %d,%d\n", machine.primary_screen->vpos(), machine.primary_screen->hpos());

	if (state->m_display_position_interrupt_control & IRQ2CTRL_ENABLE)
	{
		if (LOG_VIDEO_SYSTEM) logerror("*** Scanline interrupt (IRQ2) ***  y: %02x  x: %02x\n", machine.primary_screen->vpos(), machine.primary_screen->hpos());
		state->m_display_position_interrupt_pending = 1;

		update_interrupts(machine);
	}

	if (state->m_display_position_interrupt_control & IRQ2CTRL_AUTOLOAD_REPEAT)
	{
		if (LOG_VIDEO_SYSTEM) logerror("AUTOLOAD_REPEAT ");
		adjust_display_position_interrupt_timer(machine);
	}
}


static TIMER_CALLBACK( display_position_vblank_callback )
{
	neogeo_state *state = machine.driver_data<neogeo_state>();

	if (state->m_display_position_interrupt_control & IRQ2CTRL_AUTOLOAD_VBLANK)
	{
		if (LOG_VIDEO_SYSTEM) logerror("AUTOLOAD_VBLANK ");
		adjust_display_position_interrupt_timer(machine);
	}

	/* set timer for next screen */
	state->m_display_position_vblank_timer->adjust(machine.primary_screen->time_until_pos(NEOGEO_VBSTART, NEOGEO_VBLANK_RELOAD_HPOS));
}


static TIMER_CALLBACK( vblank_interrupt_callback )
{
	neogeo_state *state = machine.driver_data<neogeo_state>();

	if (LOG_VIDEO_SYSTEM) logerror("+++ VBLANK @ %d,%d\n", machine.primary_screen->vpos(), machine.primary_screen->hpos());

	/* add a timer tick to the pd4990a */
	upd4990a_addretrace(state->m_upd4990a);

	state->m_vblank_interrupt_pending = 1;

	update_interrupts(machine);

	/* set timer for next screen */
	state->m_vblank_interrupt_timer->adjust(machine.primary_screen->time_until_pos(NEOGEO_VBSTART, 0));
}


static void create_interrupt_timers( running_machine &machine )
{
	neogeo_state *state = machine.driver_data<neogeo_state>();
	state->m_display_position_interrupt_timer = machine.scheduler().timer_alloc(FUNC(display_position_interrupt_callback));
	state->m_display_position_vblank_timer = machine.scheduler().timer_alloc(FUNC(display_position_vblank_callback));
	state->m_vblank_interrupt_timer = machine.scheduler().timer_alloc(FUNC(vblank_interrupt_callback));
}


static void start_interrupt_timers( running_machine &machine )
{
	neogeo_state *state = machine.driver_data<neogeo_state>();
	state->m_vblank_interrupt_timer->adjust(machine.primary_screen->time_until_pos(NEOGEO_VBSTART, 0));
	state->m_display_position_vblank_timer->adjust(machine.primary_screen->time_until_pos(NEOGEO_VBSTART, NEOGEO_VBLANK_RELOAD_HPOS));
}



/*************************************
 *
 *  Audio CPU interrupt generation
 *
 *************************************/

static void audio_cpu_irq(device_t *device, int assert)
{
	neogeo_state *state = device->machine().driver_data<neogeo_state>();
	device_set_input_line(state->m_audiocpu, 0, assert ? ASSERT_LINE : CLEAR_LINE);
}


static void audio_cpu_assert_nmi(running_machine &machine)
{
	neogeo_state *state = machine.driver_data<neogeo_state>();
	device_set_input_line(state->m_audiocpu, INPUT_LINE_NMI, ASSERT_LINE);
}


static WRITE8_HANDLER( audio_cpu_clear_nmi_w )
{
	neogeo_state *state = space->machine().driver_data<neogeo_state>();
	device_set_input_line(state->m_audiocpu, INPUT_LINE_NMI, CLEAR_LINE);
}



/*************************************
 *
 *  Input ports / Controllers
 *
 *************************************/

static void select_controller( running_machine &machine, UINT8 data )
{
	neogeo_state *state = machine.driver_data<neogeo_state>();
	state->m_controller_select = data;
}


#if 0
static CUSTOM_INPUT( multiplexed_controller_r )
{
	int port = (FPTR)param;

	static const char *const cntrl[2][2] =
		{
			{ "IN0-0", "IN0-1" }, { "IN1-0", "IN1-1" }
		};

	return input_port_read_safe(field->port->machine(), cntrl[port][controller_select & 0x01], 0x00);
}


static CUSTOM_INPUT( mahjong_controller_r )
{
	UINT32 ret;

/*
cpu #0 (PC=00C18B9A): unmapped memory word write to 00380000 = 0012 & 00FF
cpu #0 (PC=00C18BB6): unmapped memory word write to 00380000 = 001B & 00FF
cpu #0 (PC=00C18D54): unmapped memory word write to 00380000 = 0024 & 00FF
cpu #0 (PC=00C18D6C): unmapped memory word write to 00380000 = 0009 & 00FF
cpu #0 (PC=00C18C40): unmapped memory word write to 00380000 = 0000 & 00FF
*/
	switch (controller_select)
	{
	default:
	case 0x00: ret = 0x0000; break; /* nothing? */
	case 0x09: ret = input_port_read(field->port->machine(), "MAHJONG1"); break;
	case 0x12: ret = input_port_read(field->port->machine(), "MAHJONG2"); break;
	case 0x1b: ret = input_port_read(field->port->machine(), "MAHJONG3"); break; /* player 1 normal inputs? */
	case 0x24: ret = input_port_read(field->port->machine(), "MAHJONG4"); break;
	}

	return ret;
}

#endif

static WRITE16_HANDLER( io_control_w )
{
	neogeo_state *state = space->machine().driver_data<neogeo_state>();
	switch (offset)
	{
	case 0x00: select_controller(space->machine(), data & 0x00ff); break;
//  case 0x18: set_output_latch(space->machine(), data & 0x00ff); break;
//  case 0x20: set_output_data(space->machine(), data & 0x00ff); break;
	case 0x28: upd4990a_control_16_w(state->m_upd4990a, 0, data, mem_mask); break;
//  case 0x30: break; // coin counters
//  case 0x31: break; // coin counters
//  case 0x32: break; // coin lockout
//  case 0x33: break; // coui lockout

	default:
		logerror("PC: %x  Unmapped I/O control write.  Offset: %x  Data: %x\n", cpu_get_pc(&space->device()), offset, data);
		break;
	}
}



/*************************************
 *
 *  Unmapped memory access
 *
 *************************************/
#ifdef MESS
READ16_HANDLER( neogeo_unmapped_r )
{
	neogeo_state *state = space->machine().driver_data<neogeo_state>();
	UINT16 ret;

	/* unmapped memory returns the last word on the data bus, which is almost always the opcode
       of the next instruction due to prefetch */

	/* prevent recursion */
	if (state->m_recurse)
		ret = 0xffff;
	else
	{
		state->m_recurse = 1;
		ret = space->read_word(cpu_get_pc(&space->device()));
		state->m_recurse = 0;
	}

	return ret;
}
#endif



/*************************************
 *
 *  uPD4990A calendar chip
 *
 *************************************/
#if 0
static void calendar_init(running_machine &machine)
{
	/* set the celander IC to 'right now' */
	system_time systime;
	system_tm time;

	machine.base_datetime(&systime);
	time = systime.local_time;

	pd4990a.seconds = ((time.second / 10) << 4) + (time.second % 10);
	pd4990a.minutes = ((time.minute / 10) << 4) + (time.minute % 10);
	pd4990a.hours = ((time.hour / 10) <<4 ) + (time.hour % 10);
	pd4990a.days = ((time.mday / 10) << 4) + (time.mday % 10);
	pd4990a.month = time.month + 1;
	pd4990a.year = ((((time.year - 1900) % 100) / 10) << 4) + ((time.year - 1900) % 10);
	pd4990a.weekday = time.weekday;
}


static void calendar_clock(void)
{
//  pd4990a_addretrace();
}
#endif

static CUSTOM_INPUT( get_calendar_status )
{
	neogeo_state *state = field->port->machine().driver_data<neogeo_state>();
	return (upd4990a_databit_r(state->m_upd4990a, 0) << 1) | upd4990a_testbit_r(state->m_upd4990a, 0);
}



/*************************************
 *
 *  NVRAM (Save RAM)
 *
 *************************************/
#if 0
static NVRAM_HANDLER( neogeo )
{
	if (read_or_write)
		/* save the SRAM settings */
		mame_fwrite(file, save_ram, 0x2000);
	else
	{
		/* load the SRAM settings */
		if (file)
			mame_fread(file, save_ram, 0x2000);
		else
			memset(save_ram, 0, 0x10000);
	}
}


static void set_save_ram_unlock( running_machine &machine, UINT8 data )
{
	neogeo_state *state = machine.driver_data<neogeo_state>();
	state->m_save_ram_unlocked = data;
}


static WRITE16_HANDLER( save_ram_w )
{
	neogeo_state *state = space->machine().driver_data<neogeo_state>();

	if (state->m_save_ram_unlocked)
		COMBINE_DATA(&save_ram[offset]);
}
#endif


/*************************************
 *
 *  Memory card
 *
 *************************************/

#define MEMCARD_SIZE	0x0800

static CUSTOM_INPUT( get_memcard_status )
{
	/* D0 and D1 are memcard presence indicators, D2 indicates memcard
       write protect status (we are always write enabled) */
	if(strcmp((char*)field->port->machine().system().name,"aes") != 0)
		return 0x00;  // On the Neo Geo CD, the memory card is internal and therefore always present.
	else
		return (memcard_present(field->port->machine()) == -1) ? 0x07 : 0x00;
}

static READ16_HANDLER( memcard_r )
{
	ng_aes_state *state = space->machine().driver_data<ng_aes_state>();
	UINT16 ret;

	if (memcard_present(space->machine()) != -1)
		ret = state->m_memcard_data[offset] | 0xff00;
	else
		ret = 0xffff;

	return ret;
}


static WRITE16_HANDLER( memcard_w )
{
	ng_aes_state *state = space->machine().driver_data<ng_aes_state>();
	if (ACCESSING_BITS_0_7)
	{
		if (memcard_present(space->machine()) != -1)
			state->m_memcard_data[offset] = data;
	}
}

/* The NeoCD has an 8kB internal memory card, instead of memcard slots like the MVS and AES */
static READ16_HANDLER( neocd_memcard_r )
{
	ng_aes_state *state = space->machine().driver_data<ng_aes_state>();
	return state->m_memcard_data[offset] | 0xff00;
}


static WRITE16_HANDLER( neocd_memcard_w )
{
	ng_aes_state *state = space->machine().driver_data<ng_aes_state>();
	if (ACCESSING_BITS_0_7)
	{
		state->m_memcard_data[offset] = data;
	}
}

static MEMCARD_HANDLER( neogeo )
{
	ng_aes_state *state = machine.driver_data<ng_aes_state>();
	switch (action)
	{
	case MEMCARD_CREATE:
		memset(state->m_memcard_data, 0, MEMCARD_SIZE);
		file.write(state->m_memcard_data, MEMCARD_SIZE);
		break;

	case MEMCARD_INSERT:
		file.read(state->m_memcard_data, MEMCARD_SIZE);
		break;

	case MEMCARD_EJECT:
		file.write(state->m_memcard_data, MEMCARD_SIZE);
		break;
	}
}



/*************************************
 *
 *  Inter-CPU communications
 *
 *************************************/

static WRITE16_HANDLER( audio_command_w )
{
	/* accessing the LSB only is not mapped */
	if (mem_mask != 0x00ff)
	{
		soundlatch_w(space, 0, data >> 8);

		audio_cpu_assert_nmi(space->machine());

		/* boost the interleave to let the audio CPU read the command */
		space->machine().scheduler().boost_interleave(attotime::zero, attotime::from_usec(50));

		if (LOG_CPU_COMM) logerror("MAIN CPU PC %06x: audio_command_w %04x - %04x\n", cpu_get_pc(&space->device()), data, mem_mask);
	}
}


static READ8_HANDLER( audio_command_r )
{
	UINT8 ret = soundlatch_r(space, 0);

	if (LOG_CPU_COMM) logerror(" AUD CPU PC   %04x: audio_command_r %02x\n", cpu_get_pc(&space->device()), ret);

	/* this is a guess */
	audio_cpu_clear_nmi_w(space, 0, 0);

	return ret;
}


static WRITE8_HANDLER( audio_result_w )
{
	neogeo_state *state = space->machine().driver_data<neogeo_state>();

	if (LOG_CPU_COMM && (state->m_audio_result != data)) logerror(" AUD CPU PC   %04x: audio_result_w %02x\n", cpu_get_pc(&space->device()), data);

	state->m_audio_result = data;
}


static CUSTOM_INPUT( get_audio_result )
{
	neogeo_state *state = field->port->machine().driver_data<neogeo_state>();
	UINT32 ret = state->m_audio_result;

//  if (LOG_CPU_COMM) logerror("MAIN CPU PC %06x: audio_result_r %02x\n", cpu_get_pc(field->port->machine().device("maincpu")), ret);

	return ret;
}



/*************************************
 *
 *  Main CPU banking
 *
 *************************************/

static void _set_main_cpu_vector_table_source( running_machine &machine )
{
	neogeo_state *state = machine.driver_data<neogeo_state>();
	memory_set_bank(machine, NEOGEO_BANK_VECTORS, state->m_main_cpu_vector_table_source);
}


static void set_main_cpu_vector_table_source( running_machine &machine, UINT8 data )
{
	neogeo_state *state = machine.driver_data<neogeo_state>();
	state->m_main_cpu_vector_table_source = data;

	_set_main_cpu_vector_table_source(machine);
}


static void _set_main_cpu_bank_address( running_machine &machine )
{
	neogeo_state *state = machine.driver_data<neogeo_state>();
	memory_set_bankptr(machine, NEOGEO_BANK_CARTRIDGE, &machine.region("maincpu")->base()[state->m_main_cpu_bank_address]);
}

#ifdef MESS
void neogeo_set_main_cpu_bank_address( address_space *space, UINT32 bank_address )
{
	neogeo_state *state = space->machine().driver_data<neogeo_state>();

	if (LOG_MAIN_CPU_BANKING) logerror("MAIN CPU PC %06x: neogeo_set_main_cpu_bank_address %06x\n", cpu_get_pc(&space->device()), bank_address);

	state->m_main_cpu_bank_address = bank_address;

	_set_main_cpu_bank_address(space->machine());
}
#endif

static WRITE16_HANDLER( main_cpu_bank_select_w )
{
	UINT32 bank_address;
	UINT32 len = space->machine().region("maincpu")->bytes();

	if ((len <= 0x100000) && (data & 0x07))
		logerror("PC %06x: warning: bankswitch to %02x but no banks available\n", cpu_get_pc(&space->device()), data);
	else
	{
		bank_address = ((data & 0x07) + 1) * 0x100000;

		if (bank_address >= len)
		{
			logerror("PC %06x: warning: bankswitch to empty bank %02x\n", cpu_get_pc(&space->device()), data);
			bank_address = 0x100000;
		}

		neogeo_set_main_cpu_bank_address(space, bank_address);
	}
}


static void main_cpu_banking_init( running_machine &machine )
{
	address_space *mainspace = machine.device("maincpu")->memory().space(AS_PROGRAM);

	/* create vector banks */
	memory_configure_bank(machine, NEOGEO_BANK_VECTORS, 0, 1, machine.region("mainbios")->base(), 0);
	memory_configure_bank(machine, NEOGEO_BANK_VECTORS, 1, 1, machine.region("maincpu")->base(), 0);

	/* set initial main CPU bank */
	if (machine.region("maincpu")->bytes() > 0x100000)
		neogeo_set_main_cpu_bank_address(mainspace, 0x100000);
	else
		neogeo_set_main_cpu_bank_address(mainspace, 0x000000);
}



/*************************************
 *
 *  Audio CPU banking
 *
 *************************************/

static void set_audio_cpu_banking( running_machine &machine )
{
	neogeo_state *state = machine.driver_data<neogeo_state>();
	int region;

	for (region = 0; region < 4; region++)
		memory_set_bank(machine, NEOGEO_BANK_AUDIO_CPU_CART_BANK + region, state->m_audio_cpu_banks[region]);
}


static void audio_cpu_bank_select( address_space *space, int region, UINT8 bank )
{
	neogeo_state *state = space->machine().driver_data<neogeo_state>();

	if (LOG_AUDIO_CPU_BANKING) logerror("Audio CPU PC %03x: audio_cpu_bank_select: Region: %d   Bank: %02x\n", cpu_get_pc(&space->device()), region, bank);

	state->m_audio_cpu_banks[region] = bank;

	set_audio_cpu_banking(space->machine());
}


static READ8_HANDLER( audio_cpu_bank_select_f000_f7ff_r )
{
	audio_cpu_bank_select(space, 0, offset >> 8);

	return 0;
}


static READ8_HANDLER( audio_cpu_bank_select_e000_efff_r )
{
	audio_cpu_bank_select(space, 1, offset >> 8);

	return 0;
}


static READ8_HANDLER( audio_cpu_bank_select_c000_dfff_r )
{
	audio_cpu_bank_select(space, 2, offset >> 8);

	return 0;
}


static READ8_HANDLER( audio_cpu_bank_select_8000_bfff_r )
{
	audio_cpu_bank_select(space, 3, offset >> 8);

	return 0;
}


static void _set_audio_cpu_rom_source( address_space *space )
{
	neogeo_state *state = space->machine().driver_data<neogeo_state>();

/*  if (!machine.region("audiobios")->base())   */
		state->m_audio_cpu_rom_source = 1;

	memory_set_bank(space->machine(), NEOGEO_BANK_AUDIO_CPU_MAIN_BANK, state->m_audio_cpu_rom_source);

	/* reset CPU if the source changed -- this is a guess */
	if (state->m_audio_cpu_rom_source != state->m_audio_cpu_rom_source_last)
	{
		state->m_audio_cpu_rom_source_last = state->m_audio_cpu_rom_source;

		cputag_set_input_line(space->machine(), "audiocpu", INPUT_LINE_RESET, PULSE_LINE);

		if (LOG_AUDIO_CPU_BANKING) logerror("Audio CPU PC %03x: selectign %s ROM\n", cpu_get_pc(&space->device()), state->m_audio_cpu_rom_source ? "CARTRIDGE" : "BIOS");
	}
}


static void set_audio_cpu_rom_source( address_space *space, UINT8 data )
{
	neogeo_state *state = space->machine().driver_data<neogeo_state>();
	state->m_audio_cpu_rom_source = data;

	_set_audio_cpu_rom_source(space);
}


static void audio_cpu_banking_init( running_machine &machine )
{
	neogeo_state *state = machine.driver_data<neogeo_state>();
	int region;
	int bank;
	UINT8 *rgn;
	UINT32 address_mask;

	/* audio bios/cartridge selection */
	if (machine.region("audiobios")->base())
		memory_configure_bank(machine, NEOGEO_BANK_AUDIO_CPU_MAIN_BANK, 0, 1, machine.region("audiobios")->base(), 0);
	memory_configure_bank(machine, NEOGEO_BANK_AUDIO_CPU_MAIN_BANK, 1, 1, machine.region("audiocpu")->base(), 0);

	/* audio banking */
	address_mask = machine.region("audiocpu")->bytes() - 0x10000 - 1;

	rgn = machine.region("audiocpu")->base();
	for (region = 0; region < 4; region++)
	{
		for (bank = 0; bank < 0x100; bank++)
		{
			UINT32 bank_address = 0x10000 + (((bank << (11 + region)) & 0x3ffff) & address_mask);
			memory_configure_bank(machine, NEOGEO_BANK_AUDIO_CPU_CART_BANK + region, bank, 1, &rgn[bank_address], 0);
		}
	}

	/* set initial audio banks --
       how does this really work, or is it even neccessary? */
	state->m_audio_cpu_banks[0] = 0x1e;
	state->m_audio_cpu_banks[1] = 0x0e;
	state->m_audio_cpu_banks[2] = 0x06;
	state->m_audio_cpu_banks[3] = 0x02;

	set_audio_cpu_banking(machine);

	state->m_audio_cpu_rom_source_last = 0;
	set_audio_cpu_rom_source(machine.device("maincpu")->memory().space(AS_PROGRAM), 0);
}



/*************************************
 *
 *  System control register
 *
 *************************************/

static WRITE16_HANDLER( system_control_w )
{
	if (ACCESSING_BITS_0_7)
	{
		UINT8 bit = (offset >> 3) & 0x01;

		switch (offset & 0x07)
		{
		default:
		case 0x00: neogeo_set_screen_dark(space->machine(), bit); break;
		case 0x01: set_main_cpu_vector_table_source(space->machine(), bit);
				   set_audio_cpu_rom_source(space, bit); /* this is a guess */
				   break;
		case 0x05: neogeo_set_fixed_layer_source(space->machine(), bit); break;
//      case 0x06: set_save_ram_unlock(space->machine(), bit); break;
		case 0x07: neogeo_set_palette_bank(space->machine(), bit); break;

		case 0x02: /* unknown - HC32 middle pin 1 */
		case 0x03: /* unknown - uPD4990 pin ? */
		case 0x04: /* unknown - HC32 middle pin 10 */
			logerror("PC: %x  Unmapped system control write.  Offset: %x  Data: %x\n", cpu_get_pc(&space->device()), offset & 0x07, bit);
			break;
		}

		if (LOG_VIDEO_SYSTEM && ((offset & 0x07) != 0x06)) logerror("PC: %x  System control write.  Offset: %x  Data: %x\n", cpu_get_pc(&space->device()), offset & 0x07, bit);
	}
}

/*
 *  CD-ROM / DMA control
 *
 *  DMA

    FF0061  Write 0x40 means start DMA transfer
    FF0064  Source address (in copy mode), Target address (in filll mode)
    FF0068  Target address (in copy mode)
    FF006C  Fill word
    FF0070  Words count
    FF007E  \
    ......   | DMA programming words?   NeoGeoCD uses Sanyo Puppet LC8359 chip to
    FF008E  /                           interface with CD, and do DMA transfers

    Memory access control

    FF011C  DIP SWITCH (Region code)
    FF0105  Area Selector (5 = FIX, 0 = SPR, 4 = Z80, 1 = PCM)
    FF01A1  Sprite bank selector
    FF01A3  PCM bank selector
    FF0120  Prepare sprite area for transfer
    FF0122  Prepare PCM area for transfer
    FF0126  Prepare Z80 area for transfer
    FF0128  Prepare Fix area for transfer
    FF0140  Terminate work on Spr Area  (Sprites must be decoded here)
    FF0142  Terminate work on Pcm Area
    FF0146  Terminate work on Z80 Area  (Z80 needs to be reset)
    FF0148  Terminate work on Fix Area

    CD-ROM:
    0xff0102 == 0xF0 start cd transfer
    int m=bcd(fast_r8(0x10f6c8));
    int s=bcd(fast_r8(0x10f6c9));
    int f=bcd(fast_r8(0x10f6ca));
    int seccount=fast_r16(0x10f688);

    inisec=((m*60)+s)*75+f;
    inisec-=150;
    dstaddr=0x111204; // this must come from somewhere

    the value @ 0x10f688 is decremented each time a sector is read until it's 0.

 *
 */

static void neocd_do_dma(address_space* space)
{
	ng_aes_state *state = space->machine().driver_data<ng_aes_state>();
	// TODO: Proper DMA timing and control
	int count;
//  UINT16 word;

	switch(state->m_neocd_ctrl.dma_mode[0])
	{
	case 0xffdd:
		for(count=0;count<state->m_neocd_ctrl.word_count;count++)
		{
			//word = space->read_word(state->m_neocd_ctrl.addr_source);
			space->write_word(state->m_neocd_ctrl.addr_source+(count*2),state->m_neocd_ctrl.fill_word);
		}
		logerror("CTRL: DMA word-fill transfer of %i bytes\n",count*2);
		break;
	case 0xfef5:
		for(count=0;count<state->m_neocd_ctrl.word_count;count++)
		{
			//word = space->read_word(state->m_neocd_ctrl.addr_source);
			space->write_word(state->m_neocd_ctrl.addr_source+(count*4),(state->m_neocd_ctrl.addr_source+(count*4)) >> 16);
			space->write_word(state->m_neocd_ctrl.addr_source+(count*4)+2,(state->m_neocd_ctrl.addr_source+(count*4)) & 0xffff);
		}
		logerror("CTRL: DMA mode 2 transfer of %i bytes\n",count*4);
		break;
	case 0xcffd:
		for(count=0;count<state->m_neocd_ctrl.word_count;count++)
		{
			//word = space->read_word(state->m_neocd_ctrl.addr_source);
			space->write_word(state->m_neocd_ctrl.addr_source+(count*8),((state->m_neocd_ctrl.addr_source+(count*8)) >> 24) | 0xff00);
			space->write_word(state->m_neocd_ctrl.addr_source+(count*8)+2,((state->m_neocd_ctrl.addr_source+(count*8)) >> 16) | 0xff00);
			space->write_word(state->m_neocd_ctrl.addr_source+(count*8)+4,((state->m_neocd_ctrl.addr_source+(count*8)) >> 8) | 0xff00);
			space->write_word(state->m_neocd_ctrl.addr_source+(count*8)+6,(state->m_neocd_ctrl.addr_source+(count*8)) | 0xff00);
		}
		logerror("CTRL: DMA mode 3 transfer of %i bytes\n",count*8);
		break;
	default:
		logerror("CTRL: Unknown DMA transfer mode %04x\n",state->m_neocd_ctrl.dma_mode[0]);
	}
}

static READ16_HANDLER( neocd_control_r )
{
	ng_aes_state *state = space->machine().driver_data<ng_aes_state>();

	switch(offset)
	{
	case 0x64/2: // source address, high word
		return (state->m_neocd_ctrl.addr_source >> 16) & 0xffff;
	case 0x66/2: // source address, low word
		return state->m_neocd_ctrl.addr_source & 0xffff;
	case 0x68/2: // target address, high word
		return (state->m_neocd_ctrl.addr_target >> 16) & 0xffff;
	case 0x6a/2: // target address, low word
		return state->m_neocd_ctrl.addr_target & 0xffff;
	case 0x6c/2: // fill word
		return state->m_neocd_ctrl.fill_word;
	case 0x70/2: // word count
		return (state->m_neocd_ctrl.word_count >> 16) & 0xffff;
	case 0x72/2:
		return state->m_neocd_ctrl.word_count & 0xffff;
	case 0x7e/2:  // DMA parameters
	case 0x80/2:
	case 0x82/2:
	case 0x84/2:
	case 0x86/2:
	case 0x88/2:
	case 0x8a/2:
	case 0x8c/2:
	case 0x8e/2:
		return state->m_neocd_ctrl.dma_mode[offset-(0x7e/2)];
		break;
	case 0x105/2:
		return state->m_neocd_ctrl.area_sel;
	case 0x11c/2:
		logerror("CTRL: Read region code.\n");
		return 0x0600;  // we'll just force USA region for now
	case 0x1a0/2:
		return state->m_neocd_ctrl.spr_bank_sel;
		break;
	case 0x1a2/2:
		return state->m_neocd_ctrl.pcm_bank_sel;
		break;
	default:
		logerror("CTRL: Read offset %04x\n",offset);
	}

	return 0;
}

static WRITE16_HANDLER( neocd_control_w )
{
	ng_aes_state *state = space->machine().driver_data<ng_aes_state>();
	switch(offset)
	{
	case 0x60/2: // Start DMA transfer
		if((data & 0xff) == 0x40)
			neocd_do_dma(space);
		break;
	case 0x64/2: // source address, high word
		state->m_neocd_ctrl.addr_source = (state->m_neocd_ctrl.addr_source & 0x0000ffff) | (data << 16);
		logerror("CTRL: Set source address to %08x\n",state->m_neocd_ctrl.addr_source);
		break;
	case 0x66/2: // source address, low word
		state->m_neocd_ctrl.addr_source = (state->m_neocd_ctrl.addr_source & 0xffff0000) | data;
		logerror("CTRL: Set source address to %08x\n",state->m_neocd_ctrl.addr_source);
		break;
	case 0x68/2: // target address, high word
		state->m_neocd_ctrl.addr_target = (state->m_neocd_ctrl.addr_target & 0x0000ffff) | (data << 16);
		logerror("CTRL: Set target address to %08x\n",state->m_neocd_ctrl.addr_target);
		break;
	case 0x6a/2: // target address, low word
		state->m_neocd_ctrl.addr_target = (state->m_neocd_ctrl.addr_target & 0xffff0000) | data;
		logerror("CTRL: Set target address to %08x\n",state->m_neocd_ctrl.addr_target);
		break;
	case 0x6c/2: // fill word
		state->m_neocd_ctrl.fill_word = data;
		logerror("CTRL: Set fill word to %04x\n",data);
		break;
	case 0x70/2: // word count
		state->m_neocd_ctrl.word_count = (state->m_neocd_ctrl.word_count & 0x0000ffff) | (data << 16);
		logerror("CTRL: Set word count to %i\n",state->m_neocd_ctrl.word_count);
		break;
	case 0x72/2: // word count (low word)
		state->m_neocd_ctrl.word_count = (state->m_neocd_ctrl.word_count & 0xffff0000) | data;
		logerror("CTRL: Set word count to %i\n",state->m_neocd_ctrl.word_count);
		break;
	case 0x7e/2:  // DMA parameters
	case 0x80/2:
	case 0x82/2:
	case 0x84/2:
	case 0x86/2:
	case 0x88/2:
	case 0x8a/2:
	case 0x8c/2:
	case 0x8e/2:
		state->m_neocd_ctrl.dma_mode[offset-(0x7e/2)] = data;
		logerror("CTRL: DMA parameter %i set to %04x\n",offset-(0x7e/2),data);
		break;
	case 0x104/2:
		state->m_neocd_ctrl.area_sel = data & 0x00ff;
		logerror("CTRL: 0xExxxxx set to area %i\n",data & 0xff);
		break;
	case 0x140/2:  // end sprite transfer
		video_reset_neogeo(space->machine());
		break;
	case 0x142/2:  // end PCM transfer
		break;
	case 0x146/2:  // end Z80 transfer
		cputag_set_input_line(space->machine(),"audiocpu",INPUT_LINE_RESET,PULSE_LINE);
		break;
	case 0x148/2:  // end FIX transfer
		video_reset_neogeo(space->machine());
		break;
	case 0x1a0/2:
		state->m_neocd_ctrl.spr_bank_sel = data & 0xff;
		logerror("CTRL: Sprite area set to bank %i\n",data & 0xff);
		break;
	case 0x1a2/2:
		state->m_neocd_ctrl.pcm_bank_sel = data & 0xff;
		logerror("CTRL: PCM area set to bank %i\n",data & 0xff);
		break;
	default:
		logerror("CTRL: Write offset %04x, data %04x\n",offset*2,data);
	}
}

/*
 *  Handling NeoCD banked RAM
 *  When the Z80 space is banked in to 0xe00000, only the low byte of each word is used
 */

static READ16_HANDLER(neocd_transfer_r)
{
	ng_aes_state *state = space->machine().driver_data<ng_aes_state>();
	UINT16 ret = 0x0000;
	UINT8* Z80 = space->machine().region("audiocpu")->base();
	UINT8* PCM = space->machine().region("ymsnd")->base();
	UINT8* FIX = space->machine().region("fixed")->base();
	UINT16* SPR = (UINT16*)space->machine().region("sprites")->base();

	switch(state->m_neocd_ctrl.area_sel)
	{
	case NEOCD_AREA_AUDIO:
		ret = Z80[offset & 0xffff] | 0xff00;
		break;
	case NEOCD_AREA_PCM:
		ret = PCM[offset + (0x100000*state->m_neocd_ctrl.pcm_bank_sel)] | 0xff00;
		break;
	case NEOCD_AREA_SPR:
		ret = SPR[offset + (0x80000*state->m_neocd_ctrl.spr_bank_sel)];
		break;
	case NEOCD_AREA_FIX:
		ret = FIX[offset & 0x1ffff] | 0xff00;
		break;
	}

	return ret;
}

static WRITE16_HANDLER(neocd_transfer_w)
{
	ng_aes_state *state = space->machine().driver_data<ng_aes_state>();
	UINT8* Z80 = space->machine().region("audiocpu")->base();
	UINT8* PCM = space->machine().region("ymsnd")->base();
	UINT8* FIX = space->machine().region("fixed")->base();
	UINT16* SPR = (UINT16*)space->machine().region("sprites")->base();

	switch(state->m_neocd_ctrl.area_sel)
	{
	case NEOCD_AREA_AUDIO:
		Z80[offset & 0xffff] = data & 0xff;
		break;
	case NEOCD_AREA_PCM:
		PCM[offset + (0x100000*state->m_neocd_ctrl.pcm_bank_sel)] = data & 0xff;
		break;
	case NEOCD_AREA_SPR:
		COMBINE_DATA(SPR+(offset + (0x80000*state->m_neocd_ctrl.spr_bank_sel)));
		break;
	case NEOCD_AREA_FIX:
		FIX[offset & 0x1ffff] = data & 0xff;
		break;
	}

}

/*
 * Handling selectable controller types
 */

static READ16_HANDLER( aes_in0_r )
{
	neogeo_state *state = space->machine().driver_data<neogeo_state>();
	UINT32 ret = 0xffff;
	UINT32 ctrl = input_port_read(space->machine(),"CTRLSEL");

	switch(ctrl & 0x0f)
	{
	case 0x00:
		ret = 0xffff;
		break;
	case 0x01:
		ret = input_port_read(space->machine(),"IN0");
		break;
	case 0x02:
		switch (state->m_controller_select)
		{
			case 0x09: ret = input_port_read(space->machine(), "MJ01_P1"); break;
			case 0x12: ret = input_port_read(space->machine(), "MJ02_P1"); break;
			case 0x1b: ret = input_port_read(space->machine(), "MJ03_P1"); break; /* player 1 normal inputs? */
			case 0x24: ret = input_port_read(space->machine(), "MJ04_P1"); break;
			default:
				ret = input_port_read(space->machine(),"IN0");
				break;
		}
		break;
	}

	return ret;
}

static READ16_HANDLER( aes_in1_r )
{
	neogeo_state *state = space->machine().driver_data<neogeo_state>();
	UINT32 ret = 0xffff;
	UINT32 ctrl = input_port_read(space->machine(),"CTRLSEL");

	switch(ctrl & 0xf0)
	{
	case 0x00:
		ret = 0xffff;
		break;
	case 0x10:
		ret = input_port_read(space->machine(),"IN1");
		break;
	case 0x20:
		switch (state->m_controller_select)
		{
			case 0x09: ret = input_port_read(space->machine(), "MJ01_P2"); break;
			case 0x12: ret = input_port_read(space->machine(), "MJ02_P2"); break;
			case 0x1b: ret = input_port_read(space->machine(), "MJ03_P2"); break; /* player 2 normal inputs? */
			case 0x24: ret = input_port_read(space->machine(), "MJ04_P2"); break;
			default:
				ret = input_port_read(space->machine(),"IN1");
				break;
		}
		break;
	}

	return ret;
}


static READ16_HANDLER(aes_in2_r)
{
	UINT32 in2 = input_port_read(space->machine(),"IN2");
	UINT32 ret = in2;
	UINT32 sel = input_port_read(space->machine(),"CTRLSEL");
	neogeo_state *state = space->machine().driver_data<neogeo_state>();

	if((sel & 0x02) && (state->m_controller_select == 0x24))
		ret ^= 0x0200;

	if((sel & 0x20) && (state->m_controller_select == 0x24))
		ret ^= 0x0800;

	return ret;
}


/*************************************
 *
 *  Machine initialization
 *
 *************************************/

static STATE_POSTLOAD( aes_postload )
{
	_set_main_cpu_bank_address(machine);
	_set_main_cpu_vector_table_source(machine);
	set_audio_cpu_banking(machine);
	_set_audio_cpu_rom_source(machine.device("maincpu")->memory().space(AS_PROGRAM));
}

static void common_machine_start(running_machine &machine)
{
	neogeo_state *state = machine.driver_data<neogeo_state>();

	/* set the BIOS bank */
	memory_set_bankptr(machine, NEOGEO_BANK_BIOS, machine.region("mainbios")->base());

	/* set the initial main CPU bank */
	main_cpu_banking_init(machine);

	/* set the initial audio CPU ROM banks */
	audio_cpu_banking_init(machine);

	create_interrupt_timers(machine);

	/* start with an IRQ3 - but NOT on a reset */
	state->m_irq3_pending = 1;

	/* get devices */
	state->m_maincpu = machine.device("maincpu");
	state->m_audiocpu = machine.device("audiocpu");
	state->m_upd4990a = machine.device("upd4990a");

	/* register state save */
	state->save_item(NAME(state->m_display_position_interrupt_control));
	state->save_item(NAME(state->m_display_counter));
	state->save_item(NAME(state->m_vblank_interrupt_pending));
	state->save_item(NAME(state->m_display_position_interrupt_pending));
	state->save_item(NAME(state->m_irq3_pending));
	state->save_item(NAME(state->m_audio_result));
	state->save_item(NAME(state->m_controller_select));
	state->save_item(NAME(state->m_main_cpu_bank_address));
	state->save_item(NAME(state->m_main_cpu_vector_table_source));
	state->save_item(NAME(state->m_audio_cpu_banks));
	state->save_item(NAME(state->m_audio_cpu_rom_source));
	state->save_item(NAME(state->m_audio_cpu_rom_source_last));
	state->save_item(NAME(state->m_save_ram_unlocked));
	state->save_item(NAME(state->m_output_data));
	state->save_item(NAME(state->m_output_latch));
	state->save_item(NAME(state->m_el_value));
	state->save_item(NAME(state->m_led1_value));
	state->save_item(NAME(state->m_led2_value));
	state->save_item(NAME(state->m_recurse));

	machine.state().register_postload(aes_postload, NULL);
}

static MACHINE_START( neogeo )
{
	ng_aes_state *state = machine.driver_data<ng_aes_state>();
	common_machine_start(machine);

	/* initialize the memcard data structure */
	state->m_memcard_data = auto_alloc_array_clear(machine, UINT8, MEMCARD_SIZE);
	state->save_pointer(NAME(state->m_memcard_data), 0x0800);
}

static MACHINE_START(neocd)
{
	ng_aes_state *state = machine.driver_data<ng_aes_state>();
	UINT8* ROM = machine.region("mainbios")->base();
	UINT8* RAM = machine.region("maincpu")->base();
	UINT8* Z80bios = machine.region("audiobios")->base();
	UINT8* FIXbios = machine.region("fixedbios")->base();
	int x;

	common_machine_start(machine);

	/* initialize the memcard data structure */
	/* NeoCD doesn't have memcard slots, rather, it has a larger internal memory which works the same */
	state->m_memcard_data = auto_alloc_array_clear(machine, UINT8, 0x2000);
	state->save_pointer(NAME(state->m_memcard_data), 0x2000);

	// copy initial 68k vectors into RAM
	memcpy(RAM,ROM,0x80);

	// copy Z80 code into Z80 space (from 0x20000)
	for(x=0;x<0x10000;x+=2)
	{
		Z80bios[x] = ROM[x+0x20001];
		Z80bios[x+1] = ROM[x+0x20000];
	}

	// copy fixed tiles into FIX space (from 0x70000)
	memcpy(FIXbios,ROM+0x70000,0x10000);
}

//static DEVICE_IMAGE_LOAD(aes_cart)
//{
//  else
//      return IMAGE_INIT_FAIL;

//  return IMAGE_INIT_PASS;
//}

/*************************************
 *
 *  Machine reset
 *
 *************************************/

static MACHINE_RESET( neogeo )
{
	neogeo_state *state = machine.driver_data<neogeo_state>();
	offs_t offs;
	address_space *space = machine.device("maincpu")->memory().space(AS_PROGRAM);

	/* reset system control registers */
	for (offs = 0; offs < 8; offs++)
		system_control_w(space, offs, 0, 0x00ff);

	machine.device("maincpu")->reset();

	neogeo_reset_rng(machine);

	start_interrupt_timers(machine);

	/* trigger the IRQ3 that was set by MACHINE_START */
	update_interrupts(machine);

	state->m_recurse = 0;

	/* AES apparently always uses the cartridge's fixed bank mode */
	neogeo_set_fixed_layer_source(machine,1);
}



/*************************************
 *
 *  Main CPU memory handlers
 *
 *************************************/

static ADDRESS_MAP_START( main_map, AS_PROGRAM, 16 )
	AM_RANGE(0x000000, 0x00007f) AM_ROMBANK(NEOGEO_BANK_VECTORS)
	AM_RANGE(0x000080, 0x0fffff) AM_ROM
	AM_RANGE(0x100000, 0x10ffff) AM_MIRROR(0x0f0000) AM_RAM
	/* some games have protection devices in the 0x200000 region, it appears to map to cart space, not surprising, the ROM is read here too */
	AM_RANGE(0x200000, 0x2fffff) AM_ROMBANK(NEOGEO_BANK_CARTRIDGE)
	AM_RANGE(0x2ffff0, 0x2fffff) AM_WRITE(main_cpu_bank_select_w)
	AM_RANGE(0x300000, 0x300001) AM_MIRROR(0x01ff7e) AM_READ(aes_in0_r)
	AM_RANGE(0x300080, 0x300081) AM_MIRROR(0x01ff7e) AM_READ_PORT("IN4")
	AM_RANGE(0x300000, 0x300001) AM_MIRROR(0x01ffe0) AM_READ(neogeo_unmapped_r) AM_WRITENOP	// AES has no watchdog
	AM_RANGE(0x320000, 0x320001) AM_MIRROR(0x01fffe) AM_READ_PORT("IN3") AM_WRITE(audio_command_w)
	AM_RANGE(0x340000, 0x340001) AM_MIRROR(0x01fffe) AM_READ(aes_in1_r)
	AM_RANGE(0x360000, 0x37ffff) AM_READ(neogeo_unmapped_r)
	AM_RANGE(0x380000, 0x380001) AM_MIRROR(0x01fffe) AM_READ(aes_in2_r)
	AM_RANGE(0x380000, 0x38007f) AM_MIRROR(0x01ff80) AM_WRITE(io_control_w)
	AM_RANGE(0x3a0000, 0x3a001f) AM_MIRROR(0x01ffe0) AM_READWRITE(neogeo_unmapped_r, system_control_w)
	AM_RANGE(0x3c0000, 0x3c0007) AM_MIRROR(0x01fff8) AM_READ(neogeo_video_register_r)
	AM_RANGE(0x3c0000, 0x3c000f) AM_MIRROR(0x01fff0) AM_WRITE(neogeo_video_register_w)
	AM_RANGE(0x3e0000, 0x3fffff) AM_READ(neogeo_unmapped_r)
	AM_RANGE(0x400000, 0x401fff) AM_MIRROR(0x3fe000) AM_READWRITE(neogeo_paletteram_r, neogeo_paletteram_w)
	AM_RANGE(0x800000, 0x800fff) AM_READWRITE(memcard_r, memcard_w)
	AM_RANGE(0xc00000, 0xc1ffff) AM_MIRROR(0x0e0000) AM_ROMBANK(NEOGEO_BANK_BIOS)
	AM_RANGE(0xd00000, 0xd0ffff) AM_MIRROR(0x0f0000) AM_READ(neogeo_unmapped_r) //AM_RAM_WRITE(save_ram_w) AM_BASE(&save_ram)
	AM_RANGE(0xe00000, 0xffffff) AM_READ(neogeo_unmapped_r)
ADDRESS_MAP_END

static ADDRESS_MAP_START( neocd_main_map, AS_PROGRAM, 16 )
	AM_RANGE(0x000000, 0x00007f) AM_RAMBANK(NEOGEO_BANK_VECTORS)
	AM_RANGE(0x000080, 0x0fffff) AM_RAM
	AM_RANGE(0x100000, 0x10ffff) AM_MIRROR(0x0f0000) AM_RAM AM_BASE(&neocd_work_ram)
	/* some games have protection devices in the 0x200000 region, it appears to map to cart space, not surprising, the ROM is read here too */
	AM_RANGE(0x200000, 0x2fffff) AM_ROMBANK(NEOGEO_BANK_CARTRIDGE)
	AM_RANGE(0x2ffff0, 0x2fffff) AM_WRITE(main_cpu_bank_select_w)
	AM_RANGE(0x300000, 0x300001) AM_MIRROR(0x01ff7e) AM_READ(aes_in0_r)
	AM_RANGE(0x300080, 0x300081) AM_MIRROR(0x01ff7e) AM_READ_PORT("IN4")
	AM_RANGE(0x300000, 0x300001) AM_MIRROR(0x01ffe0) AM_READ(neogeo_unmapped_r) AM_WRITENOP	// AES has no watchdog
	AM_RANGE(0x320000, 0x320001) AM_MIRROR(0x01fffe) AM_READ_PORT("IN3") AM_WRITE(audio_command_w)
	AM_RANGE(0x340000, 0x340001) AM_MIRROR(0x01fffe) AM_READ(aes_in1_r)
	AM_RANGE(0x360000, 0x37ffff) AM_READ(neogeo_unmapped_r)
	AM_RANGE(0x380000, 0x380001) AM_MIRROR(0x01fffe) AM_READ(aes_in2_r)
	AM_RANGE(0x380000, 0x38007f) AM_MIRROR(0x01ff80) AM_WRITE(io_control_w)
	AM_RANGE(0x3a0000, 0x3a001f) AM_MIRROR(0x01ffe0) AM_READWRITE(neogeo_unmapped_r, system_control_w)
	AM_RANGE(0x3c0000, 0x3c0007) AM_MIRROR(0x01fff8) AM_READ(neogeo_video_register_r)
	AM_RANGE(0x3c0000, 0x3c000f) AM_MIRROR(0x01fff0) AM_WRITE(neogeo_video_register_w)
	AM_RANGE(0x3e0000, 0x3fffff) AM_READ(neogeo_unmapped_r)
	AM_RANGE(0x400000, 0x401fff) AM_MIRROR(0x3fe000) AM_READWRITE(neogeo_paletteram_r, neogeo_paletteram_w)
	AM_RANGE(0x800000, 0x803fff) AM_READWRITE(neocd_memcard_r, neocd_memcard_w)
	AM_RANGE(0xc00000, 0xcfffff) AM_ROMBANK(NEOGEO_BANK_BIOS)
	AM_RANGE(0xd00000, 0xd0ffff) AM_MIRROR(0x0f0000) AM_READ(neogeo_unmapped_r) //AM_RAM_WRITE(save_ram_w) AM_BASE(&save_ram)
	AM_RANGE(0xe00000, 0xefffff) AM_READWRITE(neocd_transfer_r,neocd_transfer_w)
	AM_RANGE(0xf00000, 0xfeffff) AM_READ(neogeo_unmapped_r)
	AM_RANGE(0xff0000, 0xff01ff) AM_READWRITE(neocd_control_r, neocd_control_w) // CDROM / DMA
	AM_RANGE(0xff0200, 0xffffff) AM_READ(neogeo_unmapped_r)
ADDRESS_MAP_END


/*************************************
 *
 *  Audio CPU memory handlers
 *
 *************************************/

static ADDRESS_MAP_START( audio_map, AS_PROGRAM, 8 )
	AM_RANGE(0x0000, 0x7fff) AM_ROMBANK(NEOGEO_BANK_AUDIO_CPU_MAIN_BANK)
	AM_RANGE(0x8000, 0xbfff) AM_ROMBANK(NEOGEO_BANK_AUDIO_CPU_CART_BANK + 3)
	AM_RANGE(0xc000, 0xdfff) AM_ROMBANK(NEOGEO_BANK_AUDIO_CPU_CART_BANK + 2)
	AM_RANGE(0xe000, 0xefff) AM_ROMBANK(NEOGEO_BANK_AUDIO_CPU_CART_BANK + 1)
	AM_RANGE(0xf000, 0xf7ff) AM_ROMBANK(NEOGEO_BANK_AUDIO_CPU_CART_BANK + 0)
	AM_RANGE(0xf800, 0xffff) AM_RAM
ADDRESS_MAP_END



/*************************************
 *
 *  Audio CPU port handlers
 *
 *************************************/

static ADDRESS_MAP_START( audio_io_map, AS_IO, 8 )
  /*AM_RANGE(0x00, 0x00) AM_MIRROR(0xff00) AM_READWRITE(audio_command_r, audio_cpu_clear_nmi_w);*/  /* may not and NMI clear */
	AM_RANGE(0x00, 0x00) AM_MIRROR(0xff00) AM_READ(audio_command_r)
	AM_RANGE(0x04, 0x07) AM_MIRROR(0xff00) AM_DEVREADWRITE("ymsnd", ym2610_r, ym2610_w)
	AM_RANGE(0x08, 0x08) AM_MIRROR(0xff00) /* write - NMI enable / acknowledge? (the data written doesn't matter) */
	AM_RANGE(0x08, 0x08) AM_MIRROR(0xfff0) AM_MASK(0xfff0) AM_READ(audio_cpu_bank_select_f000_f7ff_r)
	AM_RANGE(0x09, 0x09) AM_MIRROR(0xfff0) AM_MASK(0xfff0) AM_READ(audio_cpu_bank_select_e000_efff_r)
	AM_RANGE(0x0a, 0x0a) AM_MIRROR(0xfff0) AM_MASK(0xfff0) AM_READ(audio_cpu_bank_select_c000_dfff_r)
	AM_RANGE(0x0b, 0x0b) AM_MIRROR(0xfff0) AM_MASK(0xfff0) AM_READ(audio_cpu_bank_select_8000_bfff_r)
	AM_RANGE(0x0c, 0x0c) AM_MIRROR(0xff00) AM_WRITE(audio_result_w)
	AM_RANGE(0x18, 0x18) AM_MIRROR(0xff00) /* write - NMI disable? (the data written doesn't matter) */
ADDRESS_MAP_END



/*************************************
 *
 *  Audio interface
 *
 *************************************/

static const ym2610_interface ym2610_config =
{
	audio_cpu_irq
};



/*************************************
 *
 *  Standard Neo-Geo DIPs and
 *  input port definition
 *
 *************************************/

#define STANDARD_DIPS																		\
	PORT_DIPNAME( 0x0001, 0x0001, "Test Switch" ) PORT_DIPLOCATION("SW:1")					\
	PORT_DIPSETTING(	  0x0001, DEF_STR( Off ) )											\
	PORT_DIPSETTING(	  0x0000, DEF_STR( On ) )											\
	PORT_DIPNAME( 0x0002, 0x0002, "Coin Chutes?" ) PORT_DIPLOCATION("SW:2")					\
	PORT_DIPSETTING(	  0x0000, "1?" )													\
	PORT_DIPSETTING(	  0x0002, "2?" )													\
	PORT_DIPNAME( 0x0004, 0x0004, "Autofire (in some games)" ) PORT_DIPLOCATION("SW:3")		\
	PORT_DIPSETTING(	  0x0004, DEF_STR( Off ) )											\
	PORT_DIPSETTING(	  0x0000, DEF_STR( On ) )											\
	PORT_DIPNAME( 0x0018, 0x0018, "COMM Setting (Cabinet No.)" ) PORT_DIPLOCATION("SW:4,5")	\
	PORT_DIPSETTING(	  0x0018, "1" )														\
	PORT_DIPSETTING(	  0x0008, "2" )														\
	PORT_DIPSETTING(	  0x0010, "3" )														\
	PORT_DIPSETTING(	  0x0000, "4" )														\
	PORT_DIPNAME( 0x0020, 0x0020, "COMM Setting (Link Enable)" ) PORT_DIPLOCATION("SW:6")	\
	PORT_DIPSETTING(	  0x0020, DEF_STR( Off ) )											\
	PORT_DIPSETTING(	  0x0000, DEF_STR( On ) )											\
	PORT_DIPNAME( 0x0040, 0x0040, DEF_STR( Free_Play ) ) PORT_DIPLOCATION("SW:7")			\
	PORT_DIPSETTING(	  0x0040, DEF_STR( Off ) )											\
	PORT_DIPSETTING(	  0x0000, DEF_STR( On ) )											\
	PORT_DIPNAME( 0x0080, 0x0080, "Freeze" ) PORT_DIPLOCATION("SW:8")						\
	PORT_DIPSETTING(	  0x0080, DEF_STR( Off ) )											\
	PORT_DIPSETTING(	  0x0000, DEF_STR( On ) )

#define STANDARD_IN2																				\
	PORT_START("IN2")																				\
	PORT_BIT( 0x00ff, IP_ACTIVE_LOW, IPT_UNUSED )													\
	PORT_BIT( 0x0100, IP_ACTIVE_LOW, IPT_START1 )									\
	PORT_BIT( 0x0200, IP_ACTIVE_LOW, IPT_BUTTON5) PORT_NAME("1P Select") PORT_CODE(KEYCODE_5) PORT_PLAYER(1)	\
	PORT_BIT( 0x0400, IP_ACTIVE_LOW, IPT_START2 )									\
	PORT_BIT( 0x0800, IP_ACTIVE_LOW, IPT_BUTTON5) PORT_NAME("2P Select") PORT_CODE(KEYCODE_6) PORT_PLAYER(2)	\
	PORT_BIT( 0x7000, IP_ACTIVE_HIGH, IPT_SPECIAL ) PORT_CUSTOM(get_memcard_status, NULL)			\
	PORT_BIT( 0x8000, IP_ACTIVE_HIGH, IPT_UNKNOWN )  /* Matrimelee expects this bit to be active high when on an AES */

#define STANDARD_IN3																				\
	PORT_START("IN3")																				\
	PORT_BIT( 0x0001, IP_ACTIVE_LOW, IPT_UNUSED )  /* Coin 1 - AES has no coin slots, it's a console */	\
	PORT_BIT( 0x0002, IP_ACTIVE_LOW, IPT_UNUSED )  /* Coin 2 - AES has no coin slots, it's a console */	\
	PORT_BIT( 0x0004, IP_ACTIVE_LOW, IPT_UNUSED )  /* Service Coin - not used, AES is a console */	\
	PORT_BIT( 0x0008, IP_ACTIVE_HIGH, IPT_UNKNOWN ) /* having this ACTIVE_HIGH causes you to start with 2 credits using USA bios roms */	\
	PORT_BIT( 0x0010, IP_ACTIVE_HIGH, IPT_UNKNOWN ) /* having this ACTIVE_HIGH causes you to start with 2 credits using USA bios roms */	\
	PORT_BIT( 0x0020, IP_ACTIVE_LOW, IPT_SPECIAL ) /* what is this? */								\
	PORT_BIT( 0x00c0, IP_ACTIVE_HIGH, IPT_SPECIAL ) PORT_CUSTOM(get_calendar_status, NULL)			\
	PORT_BIT( 0xff00, IP_ACTIVE_HIGH, IPT_SPECIAL ) PORT_CUSTOM(get_audio_result, NULL)


#define STANDARD_IN4																			\
	PORT_START("IN4")																			\
	PORT_BIT( 0x0001, IP_ACTIVE_HIGH, IPT_UNKNOWN )												\
	PORT_BIT( 0x0002, IP_ACTIVE_HIGH, IPT_UNKNOWN )												\
	PORT_BIT( 0x0004, IP_ACTIVE_HIGH, IPT_UNKNOWN )												\
	PORT_BIT( 0x0008, IP_ACTIVE_HIGH, IPT_UNKNOWN )												\
	PORT_BIT( 0x0010, IP_ACTIVE_HIGH, IPT_UNKNOWN )												\
	PORT_BIT( 0x0020, IP_ACTIVE_HIGH, IPT_UNKNOWN )												\
	PORT_BIT( 0x0040, IP_ACTIVE_HIGH, IPT_SPECIAL ) /* what is this? - is 0 for 1 or 2 slot MVS (and AES?)*/							\
	PORT_BIT( 0x0080, IP_ACTIVE_LOW, IPT_OTHER ) PORT_NAME("Enter BIOS") PORT_CODE(KEYCODE_F2)	\
	PORT_BIT( 0xff00, IP_ACTIVE_LOW, IPT_UNUSED )

static INPUT_PORTS_START( controller )
	PORT_START("IN0")
	PORT_BIT( 0x00ff, IP_ACTIVE_LOW, IPT_UNUSED )
	PORT_BIT( 0x0100, IP_ACTIVE_LOW, IPT_JOYSTICK_UP ) PORT_PLAYER(1) PORT_CATEGORY (1)
	PORT_BIT( 0x0200, IP_ACTIVE_LOW, IPT_JOYSTICK_DOWN ) PORT_PLAYER(1) PORT_CATEGORY (1)
	PORT_BIT( 0x0400, IP_ACTIVE_LOW, IPT_JOYSTICK_LEFT ) PORT_PLAYER(1) PORT_CATEGORY (1)
	PORT_BIT( 0x0800, IP_ACTIVE_LOW, IPT_JOYSTICK_RIGHT ) PORT_PLAYER(1) PORT_CATEGORY (1)
	PORT_BIT( 0x1000, IP_ACTIVE_LOW, IPT_BUTTON1 ) PORT_PLAYER(1) PORT_CATEGORY (1)
	PORT_BIT( 0x2000, IP_ACTIVE_LOW, IPT_BUTTON2 ) PORT_PLAYER(1) PORT_CATEGORY (1)
	PORT_BIT( 0x4000, IP_ACTIVE_LOW, IPT_BUTTON3 ) PORT_PLAYER(1) PORT_CATEGORY (1)
	PORT_BIT( 0x8000, IP_ACTIVE_LOW, IPT_BUTTON4 ) PORT_PLAYER(1) PORT_CATEGORY (1)


	PORT_START("IN1")
	PORT_BIT( 0x00ff, IP_ACTIVE_LOW, IPT_UNUSED )
	PORT_BIT( 0x0100, IP_ACTIVE_LOW, IPT_JOYSTICK_UP ) PORT_PLAYER(2) PORT_CATEGORY (2)
	PORT_BIT( 0x0200, IP_ACTIVE_LOW, IPT_JOYSTICK_DOWN ) PORT_PLAYER(2) PORT_CATEGORY (2)
	PORT_BIT( 0x0400, IP_ACTIVE_LOW, IPT_JOYSTICK_LEFT ) PORT_PLAYER(2) PORT_CATEGORY (2)
	PORT_BIT( 0x0800, IP_ACTIVE_LOW, IPT_JOYSTICK_RIGHT ) PORT_PLAYER(2) PORT_CATEGORY (2)
	PORT_BIT( 0x1000, IP_ACTIVE_LOW, IPT_BUTTON1 ) PORT_PLAYER(2) PORT_CATEGORY (2)
	PORT_BIT( 0x2000, IP_ACTIVE_LOW, IPT_BUTTON2 ) PORT_PLAYER(2) PORT_CATEGORY (2)
	PORT_BIT( 0x4000, IP_ACTIVE_LOW, IPT_BUTTON3 ) PORT_PLAYER(2) PORT_CATEGORY (2)
	PORT_BIT( 0x8000, IP_ACTIVE_LOW, IPT_BUTTON4 ) PORT_PLAYER(2) PORT_CATEGORY (2)
INPUT_PORTS_END

static INPUT_PORTS_START( mjpanel )
	PORT_START("MJ01_P1")
	PORT_BIT( 0x00ff, IP_ACTIVE_HIGH, IPT_UNUSED )
	PORT_BIT( 0x0100, IP_ACTIVE_LOW, IPT_MAHJONG_A ) PORT_PLAYER(1) PORT_CATEGORY (3)
	PORT_BIT( 0x0200, IP_ACTIVE_LOW, IPT_MAHJONG_B ) PORT_PLAYER(1) PORT_CATEGORY (3)
	PORT_BIT( 0x0400, IP_ACTIVE_LOW, IPT_MAHJONG_C ) PORT_PLAYER(1) PORT_CATEGORY (3)
	PORT_BIT( 0x0800, IP_ACTIVE_LOW, IPT_MAHJONG_D ) PORT_PLAYER(1) PORT_CATEGORY (3)
	PORT_BIT( 0x1000, IP_ACTIVE_LOW, IPT_MAHJONG_E ) PORT_PLAYER(1) PORT_CATEGORY (3)
	PORT_BIT( 0x2000, IP_ACTIVE_LOW, IPT_MAHJONG_F ) PORT_PLAYER(1) PORT_CATEGORY (3)
	PORT_BIT( 0x4000, IP_ACTIVE_LOW, IPT_MAHJONG_G ) PORT_PLAYER(1) PORT_CATEGORY (3)
	PORT_BIT( 0x8000, IP_ACTIVE_LOW, IPT_UNKNOWN )

	PORT_START("MJ02_P1")
	PORT_BIT( 0x00ff, IP_ACTIVE_HIGH, IPT_UNUSED )
	PORT_BIT( 0x0100, IP_ACTIVE_LOW, IPT_MAHJONG_H ) PORT_PLAYER(1) PORT_CATEGORY (3)
	PORT_BIT( 0x0200, IP_ACTIVE_LOW, IPT_MAHJONG_I ) PORT_PLAYER(1) PORT_CATEGORY (3)
	PORT_BIT( 0x0400, IP_ACTIVE_LOW, IPT_MAHJONG_J ) PORT_PLAYER(1) PORT_CATEGORY (3)
	PORT_BIT( 0x0800, IP_ACTIVE_LOW, IPT_MAHJONG_K ) PORT_PLAYER(1) PORT_CATEGORY (3)
	PORT_BIT( 0x1000, IP_ACTIVE_LOW, IPT_MAHJONG_L ) PORT_PLAYER(1) PORT_CATEGORY (3)
	PORT_BIT( 0x2000, IP_ACTIVE_LOW, IPT_MAHJONG_M ) PORT_PLAYER(1) PORT_CATEGORY (3)
	PORT_BIT( 0x4000, IP_ACTIVE_LOW, IPT_MAHJONG_N ) PORT_PLAYER(1) PORT_CATEGORY (3)
	PORT_BIT( 0x8000, IP_ACTIVE_LOW, IPT_BUTTON6 ) PORT_PLAYER(1) PORT_CATEGORY (3)

	PORT_START("MJ03_P1")
	PORT_BIT( 0x00ff, IP_ACTIVE_HIGH, IPT_UNUSED )
	PORT_BIT( 0x0100, IP_ACTIVE_LOW, IPT_JOYSTICK_UP ) PORT_PLAYER(1) PORT_CATEGORY (3)
	PORT_BIT( 0x0200, IP_ACTIVE_LOW, IPT_JOYSTICK_DOWN ) PORT_PLAYER(1) PORT_CATEGORY (3)
	PORT_BIT( 0x0400, IP_ACTIVE_LOW, IPT_JOYSTICK_LEFT ) PORT_PLAYER(1) PORT_CATEGORY (3)
	PORT_BIT( 0x0800, IP_ACTIVE_LOW, IPT_JOYSTICK_RIGHT ) PORT_PLAYER(1) PORT_CATEGORY (3)
	PORT_BIT( 0x1000, IP_ACTIVE_LOW, IPT_BUTTON1 ) PORT_PLAYER(1) PORT_CATEGORY (3)
	PORT_BIT( 0x2000, IP_ACTIVE_LOW, IPT_BUTTON2 ) PORT_PLAYER(1) PORT_CATEGORY (3)
	PORT_BIT( 0x4000, IP_ACTIVE_LOW, IPT_BUTTON3 ) PORT_PLAYER(1) PORT_CATEGORY (3)
	PORT_BIT( 0x8000, IP_ACTIVE_LOW, IPT_BUTTON4 ) PORT_PLAYER(1) PORT_CATEGORY (3)

	PORT_START("MJ04_P1")
	PORT_BIT( 0x00ff, IP_ACTIVE_HIGH, IPT_UNUSED )
	PORT_BIT( 0x0100, IP_ACTIVE_LOW, IPT_MAHJONG_PON ) PORT_PLAYER(1) PORT_CATEGORY (3)
	PORT_BIT( 0x0200, IP_ACTIVE_LOW, IPT_MAHJONG_CHI ) PORT_PLAYER(1) PORT_CATEGORY (3)
	PORT_BIT( 0x0400, IP_ACTIVE_LOW, IPT_MAHJONG_KAN ) PORT_PLAYER(1) PORT_CATEGORY (3)
	PORT_BIT( 0x0800, IP_ACTIVE_LOW, IPT_MAHJONG_RON ) PORT_PLAYER(1) PORT_CATEGORY (3)
	PORT_BIT( 0x1000, IP_ACTIVE_LOW, IPT_MAHJONG_REACH ) PORT_PLAYER(1) PORT_CATEGORY (3)
	PORT_BIT( 0x2000, IP_ACTIVE_LOW, IPT_UNKNOWN )
	PORT_BIT( 0x4000, IP_ACTIVE_LOW, IPT_UNKNOWN )
	PORT_BIT( 0x8000, IP_ACTIVE_LOW, IPT_UNKNOWN )

	PORT_START("MJ01_P2")
	PORT_BIT( 0x00ff, IP_ACTIVE_HIGH, IPT_UNUSED )
	PORT_BIT( 0x0100, IP_ACTIVE_LOW, IPT_MAHJONG_A ) PORT_PLAYER(2) PORT_CATEGORY (4)
	PORT_BIT( 0x0200, IP_ACTIVE_LOW, IPT_MAHJONG_B ) PORT_PLAYER(2) PORT_CATEGORY (4)
	PORT_BIT( 0x0400, IP_ACTIVE_LOW, IPT_MAHJONG_C ) PORT_PLAYER(2) PORT_CATEGORY (4)
	PORT_BIT( 0x0800, IP_ACTIVE_LOW, IPT_MAHJONG_D ) PORT_PLAYER(2) PORT_CATEGORY (4)
	PORT_BIT( 0x1000, IP_ACTIVE_LOW, IPT_MAHJONG_E ) PORT_PLAYER(2) PORT_CATEGORY (4)
	PORT_BIT( 0x2000, IP_ACTIVE_LOW, IPT_MAHJONG_F ) PORT_PLAYER(2) PORT_CATEGORY (4)
	PORT_BIT( 0x4000, IP_ACTIVE_LOW, IPT_MAHJONG_G ) PORT_PLAYER(2) PORT_CATEGORY (4)
	PORT_BIT( 0x8000, IP_ACTIVE_LOW, IPT_UNKNOWN )

	PORT_START("MJ02_P2")
	PORT_BIT( 0x00ff, IP_ACTIVE_HIGH, IPT_UNUSED )
	PORT_BIT( 0x0100, IP_ACTIVE_LOW, IPT_MAHJONG_H ) PORT_PLAYER(2) PORT_CATEGORY (4)
	PORT_BIT( 0x0200, IP_ACTIVE_LOW, IPT_MAHJONG_I ) PORT_PLAYER(2) PORT_CATEGORY (4)
	PORT_BIT( 0x0400, IP_ACTIVE_LOW, IPT_MAHJONG_J ) PORT_PLAYER(2) PORT_CATEGORY (4)
	PORT_BIT( 0x0800, IP_ACTIVE_LOW, IPT_MAHJONG_K ) PORT_PLAYER(2) PORT_CATEGORY (4)
	PORT_BIT( 0x1000, IP_ACTIVE_LOW, IPT_MAHJONG_L ) PORT_PLAYER(2) PORT_CATEGORY (4)
	PORT_BIT( 0x2000, IP_ACTIVE_LOW, IPT_MAHJONG_M ) PORT_PLAYER(2) PORT_CATEGORY (4)
	PORT_BIT( 0x4000, IP_ACTIVE_LOW, IPT_MAHJONG_N ) PORT_PLAYER(2) PORT_CATEGORY (4)
	PORT_BIT( 0x8000, IP_ACTIVE_LOW, IPT_UNKNOWN )

	PORT_START("MJ03_P2")
	PORT_BIT( 0x00ff, IP_ACTIVE_HIGH, IPT_UNUSED )
	PORT_BIT( 0x0100, IP_ACTIVE_LOW, IPT_JOYSTICK_UP ) PORT_PLAYER(2) PORT_CATEGORY (4)
	PORT_BIT( 0x0200, IP_ACTIVE_LOW, IPT_JOYSTICK_DOWN ) PORT_PLAYER(2) PORT_CATEGORY (4)
	PORT_BIT( 0x0400, IP_ACTIVE_LOW, IPT_JOYSTICK_LEFT ) PORT_PLAYER(2) PORT_CATEGORY (4)
	PORT_BIT( 0x0800, IP_ACTIVE_LOW, IPT_JOYSTICK_RIGHT ) PORT_PLAYER(2) PORT_CATEGORY (4)
	PORT_BIT( 0x1000, IP_ACTIVE_LOW, IPT_BUTTON1 ) PORT_PLAYER(2) PORT_CATEGORY (4)
	PORT_BIT( 0x2000, IP_ACTIVE_LOW, IPT_BUTTON2 ) PORT_PLAYER(2) PORT_CATEGORY (4)
	PORT_BIT( 0x4000, IP_ACTIVE_LOW, IPT_BUTTON3 ) PORT_PLAYER(2) PORT_CATEGORY (4)
	PORT_BIT( 0x8000, IP_ACTIVE_LOW, IPT_BUTTON4 ) PORT_PLAYER(2) PORT_CATEGORY (4)

	PORT_START("MJ04_P2")
	PORT_BIT( 0x00ff, IP_ACTIVE_HIGH, IPT_UNUSED )
	PORT_BIT( 0x0100, IP_ACTIVE_LOW, IPT_MAHJONG_PON ) PORT_PLAYER(2) PORT_CATEGORY (4)
	PORT_BIT( 0x0200, IP_ACTIVE_LOW, IPT_MAHJONG_CHI ) PORT_PLAYER(2) PORT_CATEGORY (4)
	PORT_BIT( 0x0400, IP_ACTIVE_LOW, IPT_MAHJONG_KAN ) PORT_PLAYER(2) PORT_CATEGORY (4)
	PORT_BIT( 0x0800, IP_ACTIVE_LOW, IPT_MAHJONG_RON ) PORT_PLAYER(2) PORT_CATEGORY (4)
	PORT_BIT( 0x1000, IP_ACTIVE_LOW, IPT_MAHJONG_REACH ) PORT_PLAYER(2) PORT_CATEGORY (4)
	PORT_BIT( 0x2000, IP_ACTIVE_LOW, IPT_UNKNOWN )
	PORT_BIT( 0x4000, IP_ACTIVE_LOW, IPT_UNKNOWN )
	PORT_BIT( 0x8000, IP_ACTIVE_LOW, IPT_UNKNOWN )

INPUT_PORTS_END

static INPUT_PORTS_START( aes )
// was there anyting in place of dipswitch in the home console?
/*  PORT_START("IN0")
    PORT_BIT( 0x00ff, IP_ACTIVE_LOW, IPT_UNUSED )
//  PORT_DIPNAME( 0x0001, 0x0001, "Test Switch" ) PORT_DIPLOCATION("SW:1")
//  PORT_DIPSETTING(      0x0001, DEF_STR( Off ) )
//  PORT_DIPSETTING(      0x0000, DEF_STR( On ) )
//  PORT_DIPNAME( 0x0002, 0x0002, "Coin Chutes?" ) PORT_DIPLOCATION("SW:2")
//  PORT_DIPSETTING(      0x0000, "1?" )
//  PORT_DIPSETTING(      0x0002, "2?" )
//  PORT_DIPNAME( 0x0004, 0x0000, "Mahjong Control Panel (or Auto Fire)" ) PORT_DIPLOCATION("SW:3")
//  PORT_DIPSETTING(      0x0004, DEF_STR( Off ) )
//  PORT_DIPSETTING(      0x0000, DEF_STR( On ) )
//  PORT_DIPNAME( 0x0018, 0x0018, "COMM Setting (Cabinet No.)" ) PORT_DIPLOCATION("SW:4,5")
//  PORT_DIPSETTING(      0x0018, "1" )
//  PORT_DIPSETTING(      0x0008, "2" )
//  PORT_DIPSETTING(      0x0010, "3" )
//  PORT_DIPSETTING(      0x0000, "4" )
//  PORT_DIPNAME( 0x0020, 0x0020, "COMM Setting (Link Enable)" ) PORT_DIPLOCATION("SW:6")
//  PORT_DIPSETTING(      0x0020, DEF_STR( Off ) )
//  PORT_DIPSETTING(      0x0000, DEF_STR( On ) )
//  PORT_DIPNAME( 0x0040, 0x0040, DEF_STR( Free_Play ) ) PORT_DIPLOCATION("SW:7")
//  PORT_DIPSETTING(      0x0040, DEF_STR( Off ) )
//  PORT_DIPSETTING(      0x0000, DEF_STR( On ) )
//  PORT_DIPNAME( 0x0080, 0x0080, "Freeze" ) PORT_DIPLOCATION("SW:8")
//  PORT_DIPSETTING(      0x0080, DEF_STR( Off ) )
//  PORT_DIPSETTING(      0x0000, DEF_STR( On ) )
    PORT_BIT( 0xff00, IP_ACTIVE_HIGH, IPT_SPECIAL ) PORT_CUSTOM(aes_controller_r, NULL) */

	PORT_START("CTRLSEL")  /* Select Controller Type */
	PORT_CATEGORY_CLASS( 0x0f, 0x01, "P1 Controller")
	PORT_CATEGORY_ITEM(  0x00, "Unconnected",			0 )
	PORT_CATEGORY_ITEM(  0x01, "NeoGeo Controller",		1 )
	PORT_CATEGORY_ITEM(  0x02, "NeoGeo Mahjong Panel",	3 )
	PORT_CATEGORY_CLASS( 0xf0, 0x10, "P2 Controller")
	PORT_CATEGORY_ITEM(  0x00, "Unconnected",		0 )
	PORT_CATEGORY_ITEM(  0x10, "NeoGeo Controller",		2 )
	PORT_CATEGORY_ITEM(  0x20, "NeoGeo Mahjong Panel",	4 )

	PORT_INCLUDE( controller )

	PORT_INCLUDE( mjpanel )

// were some of these present in the AES?!?
	STANDARD_IN2

	STANDARD_IN3

	STANDARD_IN4
INPUT_PORTS_END



/*************************************
 *
 *  Machine driver
 *
 *************************************/

static MACHINE_CONFIG_START( neogeo, ng_aes_state )

	/* basic machine hardware */
	MCFG_CPU_ADD("maincpu", M68000, NEOGEO_MAIN_CPU_CLOCK)
	MCFG_CPU_PROGRAM_MAP(main_map)

	MCFG_CPU_ADD("audiocpu", Z80, NEOGEO_AUDIO_CPU_CLOCK)
	MCFG_CPU_PROGRAM_MAP(audio_map)
	MCFG_CPU_IO_MAP(audio_io_map)

	MCFG_MACHINE_START(neogeo)
	MCFG_MACHINE_RESET(neogeo)

	/* video hardware */
	MCFG_VIDEO_START(neogeo)
	MCFG_VIDEO_RESET(neogeo)
	MCFG_DEFAULT_LAYOUT(layout_neogeo)

	MCFG_SCREEN_ADD("screen", RASTER)
	MCFG_SCREEN_FORMAT(BITMAP_FORMAT_RGB32)
	MCFG_SCREEN_RAW_PARAMS(NEOGEO_PIXEL_CLOCK, NEOGEO_HTOTAL, NEOGEO_HBEND, NEOGEO_HBSTART, NEOGEO_VTOTAL, NEOGEO_VBEND, NEOGEO_VBSTART)
	MCFG_SCREEN_UPDATE(neogeo)

	/* audio hardware */
	MCFG_SPEAKER_STANDARD_STEREO("lspeaker", "rspeaker")

	MCFG_SOUND_ADD("ymsnd", YM2610, NEOGEO_YM2610_CLOCK)
	MCFG_SOUND_CONFIG(ym2610_config)
	MCFG_SOUND_ROUTE(0, "lspeaker",  0.60)
	MCFG_SOUND_ROUTE(0, "rspeaker", 0.60)
	MCFG_SOUND_ROUTE(1, "lspeaker",  1.0)
	MCFG_SOUND_ROUTE(2, "rspeaker", 1.0)

	/* NEC uPD4990A RTC */
	MCFG_UPD4990A_ADD("upd4990a")
MACHINE_CONFIG_END

static MACHINE_CONFIG_DERIVED( aes, neogeo )

	MCFG_MEMCARD_HANDLER(neogeo)

	MCFG_AES_CARTRIDGE_ADD("aes_multicart")
//  MCFG_CARTSLOT_ADD("cart")
//  MCFG_CARTSLOT_MANDATORY

//  MCFG_CARTSLOT_LOAD(aes_cart)

	MCFG_SOFTWARE_LIST_ADD("cart_list","aes")
MACHINE_CONFIG_END

static MACHINE_CONFIG_DERIVED( neocd, neogeo )

	MCFG_CPU_MODIFY("maincpu")
	MCFG_CPU_PROGRAM_MAP(neocd_main_map)

	MCFG_MACHINE_START(neocd)
MACHINE_CONFIG_END

/*************************************
 *
 *  Driver initalization
 *
 *************************************/

static DRIVER_INIT( neogeo )
{
}


ROM_START( aes )
	ROM_REGION16_BE( 0x80000, "mainbios", 0 )
	ROM_SYSTEM_BIOS( 0, "jap-aes",   "Japan AES" )
	ROMX_LOAD("neo-po.bin",  0x00000, 0x020000, CRC(16d0c132) SHA1(4e4a440cae46f3889d20234aebd7f8d5f522e22c), ROM_GROUPWORD | ROM_REVERSE | ROM_BIOS(1))	/* AES Console (Japan) Bios */
	ROM_SYSTEM_BIOS( 1, "asia-aes",   "Asia AES" )
	ROMX_LOAD("neo-epo.bin", 0x00000, 0x020000, CRC(d27a71f1) SHA1(1b3b22092f30c4d1b2c15f04d1670eb1e9fbea07), ROM_GROUPWORD | ROM_REVERSE | ROM_BIOS(2))	/* AES Console (Asia?) Bios */
//  ROM_SYSTEM_BIOS( 2, "uni-bios_2_3","Universe Bios (Hack, Ver. 2.3)" )
//  ROMX_LOAD( "uni-bios_2_3.rom",  0x00000, 0x020000, CRC(27664eb5) SHA1(5b02900a3ccf3df168bdcfc98458136fd2b92ac0), ROM_GROUPWORD | ROM_REVERSE | ROM_BIOS(3) ) /* Universe Bios v2.3 (hack) */

	ROM_REGION( 0x200000, "maincpu", ROMREGION_ERASEFF )

	ROM_REGION( 0x20000, "audiobios", 0 )
	ROM_LOAD( "sm1.sm1", 0x00000, 0x20000, CRC(94416d67) SHA1(42f9d7ddd6c0931fd64226a60dc73602b2819dcf) )

	ROM_REGION( 0x90000, "audiocpu", ROMREGION_ERASEFF )

	ROM_REGION( 0x20000, "zoomy", 0 )
	ROM_LOAD( "000-lo.lo", 0x00000, 0x20000, CRC(5a86cff2) SHA1(5992277debadeb64d1c1c64b0a92d9293eaf7e4a) )

	ROM_REGION( 0x20000, "fixedbios", 0 )
	ROM_LOAD( "sfix.sfix", 0x000000, 0x20000, CRC(c2ea0cfd) SHA1(fd4a618cdcdbf849374f0a50dd8efe9dbab706c3) )

	ROM_REGION( 0x20000, "fixed", ROMREGION_ERASEFF )

	ROM_REGION( 0x1000000, "ymsnd", ROMREGION_ERASEFF )

	ROM_REGION( 0x1000000, "ymsnd.deltat", ROMREGION_ERASEFF )

	ROM_REGION( 0x900000, "sprites", ROMREGION_ERASEFF )
ROM_END

ROM_START( neocd )
	ROM_REGION16_BE( 0x100000, "mainbios", 0 )
	ROM_LOAD16_WORD_SWAP( "top-sp1.bin",    0x00000, 0x80000, CRC(19f0ad1a) SHA1(b3e2f8467e9a642c122428d075024a9bd1fe0679) )

	ROM_REGION( 0x200000, "maincpu", ROMREGION_ERASEFF )

	ROM_REGION( 0x100000, "audiobios", ROMREGION_ERASEFF )

	ROM_REGION( 0x10000, "audiocpu", ROMREGION_ERASEFF )

	ROM_REGION( 0x20000, "zoomy", 0 )
	ROM_LOAD( "000-lo.lo", 0x00000, 0x20000, CRC(5a86cff2) SHA1(5992277debadeb64d1c1c64b0a92d9293eaf7e4a) )

	ROM_REGION( 0x20000, "fixedbios", ROMREGION_ERASEFF )

	ROM_REGION( 0x20000, "fixed", ROMREGION_ERASEFF )

	ROM_REGION( 0x400000, "ymsnd", ROMREGION_ERASEFF )

//    NO_DELTAT_REGION

	ROM_REGION( 0x400000, "sprites", ROMREGION_ERASEFF )
ROM_END

ROM_START( neocdz )
	ROM_REGION16_BE( 0x100000, "mainbios", 0 )
	ROM_LOAD16_WORD_SWAP( "neocd.bin",    0x00000, 0x80000, CRC(df9de490) SHA1(7bb26d1e5d1e930515219cb18bcde5b7b23e2eda) )

	ROM_REGION( 0x200000, "maincpu", ROMREGION_ERASEFF )

	ROM_REGION( 0x20000, "audiobios", ROMREGION_ERASEFF )

	ROM_REGION( 0x10000, "audiocpu", ROMREGION_ERASEFF )

	ROM_REGION( 0x20000, "zoomy", 0 )
	ROM_LOAD( "000-lo.lo", 0x00000, 0x20000, CRC(5a86cff2) SHA1(5992277debadeb64d1c1c64b0a92d9293eaf7e4a) )

	ROM_REGION( 0x20000, "fixedbios", ROMREGION_ERASEFF )

	ROM_REGION( 0x20000, "fixed", ROMREGION_ERASEFF )

	ROM_REGION( 0x400000, "ymsnd", ROMREGION_ERASEFF )

//    NO_DELTAT_REGION

	ROM_REGION( 0x400000, "sprites", ROMREGION_ERASEFF )
ROM_END

/*    YEAR  NAME  PARENT COMPAT MACHINE INPUT  INIT     COMPANY      FULLNAME            FLAGS */
CONS( 1990, aes,    0,   0,   aes,      aes,   neogeo,  "SNK", "Neo-Geo AES", 0)
CONS( 1994, neocd,  aes, 0,   neocd,    aes,   neogeo,  "SNK", "Neo-Geo CD", GAME_NOT_WORKING )
CONS( 1996, neocdz, aes, 0,   neocd,    aes,   neogeo,  "SNK", "Neo-Geo CDZ", GAME_NOT_WORKING )
