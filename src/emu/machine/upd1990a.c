/**********************************************************************

    NEC uPD1990AC Serial I/O Calendar & Clock emulation

    Copyright MESS Team.
    Visit http://mamedev.org for licensing and usage restrictions.

**********************************************************************/

/*

    TODO:

    - test mode

*/

#include "emu.h"
#include "upd1990a.h"
#include "machine/devhelpr.h"



//**************************************************************************
//  MACROS / CONSTANTS
//**************************************************************************

#define LOG 0


// operating modes
enum
{
	MODE_REGISTER_HOLD = 0,
	MODE_SHIFT,
	MODE_TIME_SET,
	MODE_TIME_READ,
	MODE_TP_64HZ_SET,
	MODE_TP_256HZ_SET,
	MODE_TP_2048HZ_SET,
	MODE_TEST,
};



//**************************************************************************
//  GLOBAL VARIABLES
//**************************************************************************

// devices
const device_type UPD1990A = upd1990a_device_config::static_alloc_device_config;



//**************************************************************************
//  DEVICE CONFIGURATION
//**************************************************************************

GENERIC_DEVICE_CONFIG_SETUP(upd1990a, "uPD1990A")


//-------------------------------------------------
//  device_config_complete - perform any
//  operations now that the configuration is
//  complete
//-------------------------------------------------

void upd1990a_device_config::device_config_complete()
{
	// inherit a copy of the static data
	const upd1990a_interface *intf = reinterpret_cast<const upd1990a_interface *>(static_config());
	if (intf != NULL)
		*static_cast<upd1990a_interface *>(this) = *intf;

	// or initialize to defaults if none provided
	else
	{
//      memset(&in_pa_func, 0, sizeof(in_pa_func));
	}
}



//**************************************************************************
//  INLINE HELPERS
//**************************************************************************

//-------------------------------------------------
//  convert_to_bcd -
//-------------------------------------------------

inline UINT8 upd1990a_device::convert_to_bcd(int val)
{
	return ((val / 10) << 4) | (val % 10);
}


//-------------------------------------------------
//  bcd_to_integer -
//-------------------------------------------------

inline int upd1990a_device::bcd_to_integer(UINT8 val)
{
	return (((val & 0xf0) >> 4) * 10) + (val & 0x0f);
}


//-------------------------------------------------
//  advance_seconds -
//-------------------------------------------------

inline void upd1990a_device::advance_seconds()
{
	static const int days_per_month[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };

	int seconds = bcd_to_integer(m_time_counter[0]);
	int minutes = bcd_to_integer(m_time_counter[1]);
	int hours = bcd_to_integer(m_time_counter[2]);
	int days = bcd_to_integer(m_time_counter[3]);
	int day_of_week = m_time_counter[4] & 0x0f;
	int month = (m_time_counter[4] & 0xf0) >> 4;

	seconds++;

	if (seconds > 59)
	{
		seconds = 0;
		minutes++;
	}

	if (minutes > 59)
	{
		minutes = 0;
		hours++;
	}

	if (hours > 23)
	{
		hours = 0;
		days++;
		day_of_week++;
	}

	if (day_of_week > 6)
	{
		day_of_week++;
	}

	if (days > days_per_month[month - 1])
	{
		days = 1;
		month++;
	}

	if (month > 12)
	{
		month = 1;
	}

	m_time_counter[0] = convert_to_bcd(seconds);
	m_time_counter[1] = convert_to_bcd(minutes);
	m_time_counter[2] = convert_to_bcd(hours);
	m_time_counter[3] = convert_to_bcd(days);
	m_time_counter[4] = (month << 4) | day_of_week;
}



//**************************************************************************
//  LIVE DEVICE
//**************************************************************************

//-------------------------------------------------
//  upd1990a_device - constructor
//-------------------------------------------------

upd1990a_device::upd1990a_device(running_machine &_machine, const upd1990a_device_config &config)
    : device_t(_machine, config),
      m_config(config)
{
}


//-------------------------------------------------
//  device_start - device-specific startup
//-------------------------------------------------

void upd1990a_device::device_start()
{
	// resolve callbacks
	devcb_resolve_write_line(&m_out_data_func, &m_config.m_out_data_func, this);
	devcb_resolve_write_line(&m_out_tp_func, &m_config.m_out_tp_func, this);

	// allocate timers
	m_timer_clock = timer_alloc(TIMER_CLOCK);
	m_timer_clock->adjust(attotime::zero, 0, attotime::from_hz(1));
	m_timer_tp = timer_alloc(TIMER_TP);
	m_timer_data_out = timer_alloc(TIMER_DATA_OUT);

	// state saving
	save_item(NAME(m_time_counter));
	save_item(NAME(m_shift_reg));
	save_item(NAME(m_oe));
	save_item(NAME(m_cs));
	save_item(NAME(m_stb));
	save_item(NAME(m_data_in));
	save_item(NAME(m_data_out));
	save_item(NAME(m_c));
	save_item(NAME(m_clk));
	save_item(NAME(m_tp));
}


//-------------------------------------------------
//  device_reset - device-specific reset
//-------------------------------------------------

void upd1990a_device::device_reset()
{
	system_time curtime, *systime = &curtime;

	machine().current_datetime(curtime);

	// HACK: load time counter from system time
	m_time_counter[0] = convert_to_bcd(systime->local_time.second);
	m_time_counter[1] = convert_to_bcd(systime->local_time.minute);
	m_time_counter[2] = convert_to_bcd(systime->local_time.hour);
	m_time_counter[3] = convert_to_bcd(systime->local_time.mday);
	m_time_counter[4] = systime->local_time.weekday;
	m_time_counter[4] |= (systime->local_time.month + 1) << 4;
}


//-------------------------------------------------
//  device_timer - handler timer events
//-------------------------------------------------

void upd1990a_device::device_timer(emu_timer &timer, device_timer_id id, int param, void *ptr)
{
	switch (id)
	{
	case TIMER_CLOCK:
		advance_seconds();
		break;

	case TIMER_TP:
		m_tp = !m_tp;

		if (LOG) logerror("UPD1990A TP %u\n", m_tp);

		devcb_call_write_line(&m_out_tp_func, m_tp);
		break;

	case TIMER_DATA_OUT:
		m_data_out = !m_data_out;

		if (LOG) logerror("UPD1990A DATA OUT TICK %u\n", m_data_out);

		devcb_call_write_line(&m_out_data_func, m_data_out);
		break;
	}
}


//-------------------------------------------------
//  oe_w -
//-------------------------------------------------

WRITE_LINE_MEMBER( upd1990a_device::oe_w )
{
	if (LOG) logerror("UPD1990A OE %u\n", state);

	m_oe = state;
}


//-------------------------------------------------
//  cs_w -
//-------------------------------------------------

WRITE_LINE_MEMBER( upd1990a_device::cs_w )
{
	if (LOG) logerror("UPD1990A CS %u\n", state);

	m_cs = state;
}


//-------------------------------------------------
//  stb_w -
//-------------------------------------------------

WRITE_LINE_MEMBER( upd1990a_device::stb_w )
{
	if (LOG) logerror("UPD1990A STB %u\n", state);

	m_stb = state;

	if (m_cs && m_stb && !m_clk)
	{
		switch (m_c)
		{
		case MODE_REGISTER_HOLD:
			if (LOG) logerror("UPD1990A Register Hold Mode\n");

			/* enable time counter */
			m_timer_clock->enable(1);

			/* 1 Hz data out pulse */
			m_data_out = 1;
			m_timer_data_out->adjust(attotime::zero, 0, attotime::from_hz(1*2));

			/* 64 Hz time pulse */
			m_timer_tp->adjust(attotime::zero, 0, attotime::from_hz(64*2));
			break;

		case MODE_SHIFT:
			if (LOG) logerror("UPD1990A Shift Mode\n");

			/* enable time counter */
			m_timer_clock->enable(1);

			/* disable data out pulse */
			m_timer_data_out->enable(0);

			/* output LSB of shift register */
			m_data_out = BIT(m_shift_reg[0], 0);
			devcb_call_write_line(&m_out_data_func, m_data_out);

			/* 32 Hz time pulse */
			m_timer_tp->adjust(attotime::zero, 0, attotime::from_hz(32*2));
			break;

		case MODE_TIME_SET:
			{
			int i;

			if (LOG) logerror("UPD1990A Time Set Mode\n");
			if (LOG) logerror("UPD1990A Shift Register %02x%02x%02x%02x%02x\n", m_shift_reg[4], m_shift_reg[3], m_shift_reg[2], m_shift_reg[1], m_shift_reg[0]);

			/* disable time counter */
			m_timer_clock->enable(0);

			/* disable data out pulse */
			m_timer_data_out->enable(0);

			/* output LSB of shift register */
			m_data_out = BIT(m_shift_reg[0], 0);
			devcb_call_write_line(&m_out_data_func, m_data_out);

			/* load shift register data into time counter */
			for (i = 0; i < 5; i++)
			{
				m_time_counter[i] = m_shift_reg[i];
			}

			/* 32 Hz time pulse */
			m_timer_tp->adjust(attotime::zero, 0, attotime::from_hz(32*2));
			}
			break;

		case MODE_TIME_READ:
			{
			int i;

			if (LOG) logerror("UPD1990A Time Read Mode\n");

			/* enable time counter */
			m_timer_clock->enable(1);

			/* load time counter data into shift register */
			for (i = 0; i < 5; i++)
			{
				m_shift_reg[i] = m_time_counter[i];
			}

			if (LOG) logerror("UPD1990A Shift Register %02x%02x%02x%02x%02x\n", m_shift_reg[4], m_shift_reg[3], m_shift_reg[2], m_shift_reg[1], m_shift_reg[0]);

			/* 512 Hz data out pulse */
			m_data_out = 1;
			m_timer_data_out->adjust(attotime::zero, 0, attotime::from_hz(512*2));

			/* 32 Hz time pulse */
			m_timer_tp->adjust(attotime::zero, 0, attotime::from_hz(32*2));
			}
			break;

		case MODE_TP_64HZ_SET:
			if (LOG) logerror("UPD1990A TP = 64 Hz Set Mode\n");

			/* 64 Hz time pulse */
			m_timer_tp->adjust(attotime::zero, 0, attotime::from_hz(64*2));
			break;

		case MODE_TP_256HZ_SET:
			if (LOG) logerror("UPD1990A TP = 256 Hz Set Mode\n");

			/* 256 Hz time pulse */
			m_timer_tp->adjust(attotime::zero, 0, attotime::from_hz(256*2));
			break;

		case MODE_TP_2048HZ_SET:
			if (LOG) logerror("UPD1990A TP = 2048 Hz Set Mode\n");

			/* 2048 Hz time pulse */
			m_timer_tp->adjust(attotime::zero, 0, attotime::from_hz(2048*2));
			break;

		case MODE_TEST:
			if (LOG) logerror("UPD1990A Test Mode not supported!\n");

			if (m_oe)
			{
				/* time counter is advanced at 1024 Hz from "Second" counter input */
			}
			else
			{
				/* each counter is advanced at 1024 Hz in parallel, overflow carry does not affect next counter */
			}

			break;
		}
	}
}


//-------------------------------------------------
//  clk_w -
//-------------------------------------------------

WRITE_LINE_MEMBER( upd1990a_device::clk_w )
{
	if (LOG) logerror("UPD1990A CLK %u\n", state);

	if (!m_clk && state) // rising edge
	{
		if (m_c == MODE_SHIFT)
		{
			m_shift_reg[0] >>= 1;
			m_shift_reg[0] |= (BIT(m_shift_reg[1], 0) << 7);

			m_shift_reg[1] >>= 1;
			m_shift_reg[1] |= (BIT(m_shift_reg[2], 0) << 7);

			m_shift_reg[2] >>= 1;
			m_shift_reg[2] |= (BIT(m_shift_reg[3], 0) << 7);

			m_shift_reg[3] >>= 1;
			m_shift_reg[3] |= (BIT(m_shift_reg[4], 0) << 7);

			m_shift_reg[4] >>= 1;
			m_shift_reg[4] |= (m_data_in << 7);

			if (m_oe)
			{
				m_data_out = BIT(m_shift_reg[0], 0);

				if (LOG) logerror("UPD1990A DATA OUT %u\n", m_data_out);

				devcb_call_write_line(&m_out_data_func, m_data_out);
			}
		}
	}

	m_clk = state;
}


//-------------------------------------------------
//  c0_w -
//-------------------------------------------------

WRITE_LINE_MEMBER( upd1990a_device::c0_w )
{
	if (LOG) logerror("UPD1990A C0 %u\n", state);

	m_c = (m_c & 0x06) | state;
}


//-------------------------------------------------
//  c1_w -
//-------------------------------------------------

WRITE_LINE_MEMBER( upd1990a_device::c1_w )
{
	if (LOG) logerror("UPD1990A C1 %u\n", state);

	m_c = (m_c & 0x05) | (state << 1);
}


//-------------------------------------------------
//  c2_w -
//-------------------------------------------------

WRITE_LINE_MEMBER( upd1990a_device::c2_w )
{
	if (LOG) logerror("UPD1990A C2 %u\n", state);

	m_c = (m_c & 0x03) | (state << 2);
}


//-------------------------------------------------
//  data_in_w -
//-------------------------------------------------

WRITE_LINE_MEMBER( upd1990a_device::data_in_w )
{
	if (LOG) logerror("UPD1990A DATA IN %u\n", state);

	m_data_in = state;
}


//-------------------------------------------------
//  data_out_r -
//-------------------------------------------------

READ_LINE_MEMBER( upd1990a_device::data_out_r )
{
	return m_data_out;
}


//-------------------------------------------------
//  tp_r -
//-------------------------------------------------

READ_LINE_MEMBER( upd1990a_device::tp_r )
{
	return m_tp;
}
