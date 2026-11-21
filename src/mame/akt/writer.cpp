// license:BSD-3-Clause
// copyright-holders:Sandro Ronco, Bartman/Abyss, SamDelorean
/***************************************************************************

    AKT Inc. "The Writer" (Writer Instructor), firmware v3.70

    Minimal skeleton driver.

    Based on existing MAME drivers (BSD-3-Clause):
      - src/mame/brother/ax145.cpp      : Z180 family CPU + HD44780 word processor structure
      - src/mame/skeleton/alphasma.cpp  : 40x4 LCD made of two KS0066 (HD44780 compatible)

    Hardware (known):
      - CPU: Zilog Z8S180 10VSG (10 MHz part), crystal Y2 ECS 6.144 MHz
      - ROM: M27C801 EPROM, 1 MiB (dumped, 3 identical reads)
      - RAM: 2x Samsung K6X1008 (128 KiB each, 256 KiB total)
      - Logic: 74HC139, 74HC138, 74HC86, 74HC374 (kbd rows), 2x 74VHC244 (kbd cols)
      - LCD: 40x4 characters, same module as AlphaSmart Pro / 2000 / 3000 (8-pin LCD1)
      - J1: RJ-type serial connector; PIC + 32.768 kHz crystal Y1 (role pending)

    Firmware MMU setup at reset (0x23EC): CBAR=88/CBR=B8 transition, then
    CBAR=F0, CBR=C0, BBR=C0 -> logical 0000-FFFF = physical C0000-CFFFF.

    DIAGNOSTIC BUILD: the whole 1 MiB physical space is a RAM shadow loaded
    from the ROM, so the firmware can run while we log where it writes
    (= where the real RAM must be) and which external I/O ports it uses.

    Keyboard (confirmed, routine 0CF6): direct matrix scanned by the Z180.
      - OUT (0040h) = one-hot row strobe (80h,40h,...,01h) via 74HC374
      - IN  (0080h) = sense columns 0-7  (74VHC244 #1), pressed = 1
      - IN  (0082h) = sense columns 8-15 (74VHC244 #2)
    Master-reset key combos (held at power-on, checked before the memory test):
      - 23AC: row2/col6 + row6/col6
      - 23BC: row2/col6 + row2/col8 + row6/col6
    Physical keycap names for each (row,col) still to be mapped.

    CONFIRMED by live trace + code at 07C6-088C (CGRAM-define routine):
      - RAM is physically resident at C0000-CDFFF (heavy MEMWR activity there).
      - LCD: two independent KS0066 controllers, classic two-register scheme,
        RS = low address bit (A0), controller select = next bit:
          00C0/00C1 = controller 0 instruction/data
          00C2/00C3 = controller 1 instruction/data
        (not a port-pair broadcast to both, as first assumed).

    TODO (pending firmware analysis, no data invented here):
      - map matrix (row,col) positions to physical keys
      - PIC microcontroller role / protocol on ASCI1

***************************************************************************/

#include "emu.h"

#include "cpu/z180/z180.h"
#include "video/hd44780.h"

#include "emupal.h"
#include "screen.h"


namespace {

class writer_state : public driver_device
{
public:
	writer_state(const machine_config &mconfig, device_type type, const char *tag)
		: driver_device(mconfig, type, tag)
		, m_maincpu(*this, "maincpu")
		, m_lcdc(*this, "ks0066_%u", 0U)
		, m_rom(*this, "maincpu")
		, m_kb(*this, "ROW%u", 0U)
	{ }

	void writer(machine_config &config);

protected:
	virtual void machine_start() override ATTR_COLD;

private:
	required_device<z8s180_device> m_maincpu;
	required_device_array<ks0066_device, 2> m_lcdc;
	std::unique_ptr<bitmap_ind16> m_tmp_bitmap;
	required_region_ptr<uint8_t> m_rom;
	required_ioport_array<8> m_kb;
	std::unique_ptr<uint8_t[]> m_mem;
	uint32_t m_wr_pages[256] = { };
	uint8_t m_kb_strobe = 0;

	uint8_t mem_r(offs_t offset) { return m_mem[offset]; }
	void mem_w(offs_t offset, uint8_t data);
	uint16_t kb_sense();
	void kb_strobe_w(uint8_t data) { m_kb_strobe = data; }
	uint8_t kb_sense_lo_r() { return kb_sense() & 0xff; }
	uint8_t kb_sense_hi_r() { return kb_sense() >> 8; }
	uint8_t ext_io_r(offs_t offset);
	void ext_io_w(offs_t offset, uint8_t data);
	uint8_t lcd_r(offs_t offset);
	void lcd_w(offs_t offset, uint8_t data);
	void report_writes();
	virtual void machine_reset() override ATTR_COLD;

	void map_program(address_map &map) ATTR_COLD;
	void map_io(address_map &map) ATTR_COLD;
	void palette_init(palette_device &palette) const;
	uint32_t screen_update(screen_device &screen, bitmap_ind16 &bitmap, const rectangle &cliprect);
};


void writer_state::map_program(address_map &map)
{
	// DIAGNOSTIC: RAM shadow of the ROM over the whole physical space
	map(0x00000, 0xfffff).rw(FUNC(writer_state::mem_r), FUNC(writer_state::mem_w));
}

void writer_state::map_io(address_map &map)
{
	map.global_mask(0xffff);
	map(0x0000, 0x003f).noprw(); // Z180 internal registers (handled by the CPU core)
	map(0x0040, 0xffff).rw(FUNC(writer_state::ext_io_r), FUNC(writer_state::ext_io_w)); // DIAGNOSTIC: log all other external ports
	// Keyboard matrix (confirmed, routine 0CF6) overrides the logging handlers:
	map(0x0040, 0x0040).w(FUNC(writer_state::kb_strobe_w));   // row strobe (74HC374)
	map(0x0080, 0x0080).r(FUNC(writer_state::kb_sense_lo_r)); // columns 0-7  (74VHC244 #1)
	map(0x0082, 0x0082).r(FUNC(writer_state::kb_sense_hi_r)); // columns 8-15 (74VHC244 #2)
	// LCD (confirmed, live trace): two KS0066 controllers at 00C0/00C2, same
	// RS/E wiring pattern as alphasma.cpp's update_lcdc/lcd_ctrl_w.
	map(0x00c0, 0x00c3).rw(FUNC(writer_state::lcd_r), FUNC(writer_state::lcd_w));
}

uint16_t writer_state::kb_sense()
{
	// m_kb_strobe is one-hot: bit7 -> ROW0, bit6 -> ROW1, ... bit0 -> ROW7
	uint16_t v = 0;
	for (int i = 0; i < 8; i++)
		if (BIT(m_kb_strobe, 7 - i))
			v |= m_kb[i]->read();
	return v;
}

void writer_state::mem_w(offs_t offset, uint8_t data)
{
	uint32_t &cnt = m_wr_pages[offset >> 12];
	if (cnt == 0)
		logerror("MEMWR first write to page %05X-%05X (addr %05X = %02X) PC=%04X\n", offset & 0xff000, (offset & 0xff000) | 0xfff, offset, data, m_maincpu->pc());
	cnt++;
	m_mem[offset] = data;
}

uint8_t writer_state::ext_io_r(offs_t offset)
{
	const offs_t port = offset + 0x40;
	if (!machine().side_effects_disabled())
		logerror("IORD %04X PC=%04X\n", port, m_maincpu->pc());
	return 0xff;
}

void writer_state::ext_io_w(offs_t offset, uint8_t data)
{
	logerror("IOWR %04X = %02X PC=%04X\n", offset + 0x40, data, m_maincpu->pc());
}

// LCD at 00C0-00C3 (confirmed by code at 07C6-088C, routine that defines a
// CGRAM character): standard HD44780 two-register scheme where RS is the
// low address bit (A0) and the two controllers sit at consecutive even
// bases, NOT a port-pair broadcast to both:
//   00C0 = controller 0, instruction/busy (RS=0)
//   00C1 = controller 0, data            (RS=1)
//   00C2 = controller 1, instruction/busy (RS=0)
//   00C3 = controller 1, data            (RS=1)
// (CALL 07CAh writes the even/command port as-is; CALL 07CEh does
//  INC BC; OUT (C),A; DEC BC, i.e. writes to base+1 = the data port.)
uint8_t writer_state::lcd_r(offs_t offset)
{
	unsigned ctrl = BIT(offset, 1);
	bool rs = BIT(offset, 0);
	return m_lcdc[ctrl]->read(rs);
}

void writer_state::lcd_w(offs_t offset, uint8_t data)
{
	unsigned ctrl = BIT(offset, 1);
	bool rs = BIT(offset, 0);
	if (rs)
		logerror("LCDDATA ctrl=%u data=%02X ('%c') PC=%04X\n", ctrl, data,
			(data >= 0x20 && data < 0x7f) ? data : '.', m_maincpu->pc());
	m_lcdc[ctrl]->write(rs, data);
}

void writer_state::report_writes()
{
	logerror("MEMWR summary (4 KiB pages with writes):\n");
	for (int i = 0; i < 256; i++)
		if (m_wr_pages[i])
			logerror("  %05X-%05X : %u writes\n", i << 12, (i << 12) | 0xfff, m_wr_pages[i]);
}


void writer_state::palette_init(palette_device &palette) const
{
	// same colors as alphasma.cpp
	palette.set_pen_color(0, rgb_t(138, 146, 148));
	palette.set_pen_color(1, rgb_t(92, 83, 88));
}

uint32_t writer_state::screen_update(screen_device &screen, bitmap_ind16 &bitmap, const rectangle &cliprect)
{
	// upper two lines from controller 0, lower two from controller 1 (as in alphasma.cpp)
	m_lcdc[0]->screen_update(screen, *m_tmp_bitmap, cliprect);
	copybitmap(bitmap, *m_tmp_bitmap, 0, 0, 0, 0, cliprect);
	m_lcdc[1]->screen_update(screen, *m_tmp_bitmap, cliprect);
	copybitmap(bitmap, *m_tmp_bitmap, 0, 0, 0, 18, cliprect);
	return 0;
}

void writer_state::machine_start()
{
	m_tmp_bitmap = std::make_unique<bitmap_ind16>(6 * 40, 9 * 4);
	m_mem = std::make_unique<uint8_t[]>(0x100000);
	machine().add_notifier(MACHINE_NOTIFY_EXIT, machine_notify_delegate(&writer_state::report_writes, this));
}

void writer_state::machine_reset()
{
	std::copy_n(&m_rom[0], 0x100000, &m_mem[0]);
}


// Matrix is active-high (pressed = 1). Undefined bits read 0.
// Only the keys needed to reach the master-reset screen are mapped so far;
// the remaining (row,col) positions still need their physical keycaps identified.
static INPUT_PORTS_START( writer )
	PORT_START("ROW0")
	PORT_START("ROW1")
	PORT_START("ROW2")
	PORT_BIT(0x0040, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("MR key 1 (row2,col6)")  PORT_CODE(KEYCODE_F1)
	PORT_BIT(0x0100, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("MR key 3 (row2,col8)")  PORT_CODE(KEYCODE_F3)
	PORT_START("ROW3")
	PORT_START("ROW4")
	PORT_START("ROW5")
	PORT_START("ROW6")
	PORT_BIT(0x0040, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("MR key 2 (row6,col6)")  PORT_CODE(KEYCODE_F2)
	PORT_START("ROW7")
INPUT_PORTS_END


void writer_state::writer(machine_config &config)
{
	Z8S180(config, m_maincpu, 6'144'000); // crystal Y2 ECS 6.144 MHz; CCR/CMR set by firmware
	m_maincpu->set_addrmap(AS_PROGRAM, &writer_state::map_program);
	m_maincpu->set_addrmap(AS_IO, &writer_state::map_io);

	for (auto &lcdc : m_lcdc)
	{
		KS0066(config, lcdc, 270'000); // TODO: clock not measured, datasheet typical clock used
		lcdc->set_lcd_size(2, 40);
	}

	screen_device &screen(SCREEN(config, "screen").set_lcd());
	screen.set_refresh_hz(50);
	screen.set_vblank_time(ATTOSECONDS_IN_USEC(2500));
	screen.set_screen_update(FUNC(writer_state::screen_update));
	screen.set_size(6 * 40, 9 * 4);
	screen.set_visarea_full();
	screen.set_palette("palette");

	PALETTE(config, "palette", FUNC(writer_state::palette_init), 2);
}


ROM_START( akwriter )
	ROM_REGION( 0x100000, "maincpu", 0 ) // physical region read by the shadow; no direct ROM mapping in diagnostic build
	ROM_LOAD( "akt_writer_v3.70_m27c801.bin", 0x000000, 0x100000, CRC(c454875c) SHA1(6bd0691f96c7987b2e266e220834bf4d7e3e22b4) )
ROM_END

} // anonymous namespace


//    YEAR  NAME      PARENT  COMPAT  MACHINE  INPUT   CLASS         INIT        COMPANY     FULLNAME                       FLAGS
COMP( 2005, akwriter, 0,      0,      writer,  writer, writer_state, empty_init, "AKT Inc.", "The Writer (v3.70)", MACHINE_NOT_WORKING | MACHINE_NO_SOUND )
