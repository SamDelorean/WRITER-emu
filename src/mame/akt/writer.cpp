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

    PIC (U11), confirmed by a physical dump of the real chip (Arduino +
    Willem programmer, DS30190 verify-mode protocol, 3 identical reads):
    PIC16C54C-04I/P, CONFIG=0xFFC (CP=1 unprotected, WDTE=1, FOSC=LP ->
    confirms the 32.768 kHz crystal Y1). 227 real words at 0x000-0x0E2,
    rest blank; reset vector 0x1FF -> GOTO 0x074.

    Disassembly of the real dump shows the PIC is NOT a passive slave: it
    runs a free loop keeping its own 40-bit software tick counter (bytes
    0x08-0x0B + 0x0C), and on a PORTB handshake condition it bit-bangs
    that 4-byte counter out serially on PORTB,0 (-> Z180 ASCI1 RX). It also
    drives TMR0 from an external clock, OPTION=0x20 (T0CS=1, T0SE=0,
    prescaler 1:2 on Timer0). CORRECTED (Ghidra disasm, analysis_20261121,
    confirmed on the real dump): RA2 (pin 1) and T0CKI (pin 3) are
    DISTINCT physical PIC pins, not one signal as an earlier comment here
    assumed. The firmware does pulse RA2 (PORTA bits cleared/set at PIC
    words 0x0C1-0x0C2, alongside RA3), but nothing in the disassembly ties
    that pulse to the keyboard strobe - the real matrix-scan routine
    (ROM 0x0CF6) uses Z180 ports 0x40/0x80/0x82 only, entirely separate
    from the PIC. Do not re-label RA2 as a "keyboard clock" without
    following the trace on the board. Full details + address-by-address
    disassembly: confidential doc WRITER_RE_05_PIC_desensamblado_real;
    cross-checked against WRITER_RE_08_IRQ1_vector_Ghidra (2026-11-23).

    This build adds the REAL pic16c5x core (MAME's own PIC16C54 device,
    src/devices/cpu/pic16c5x) running the real dump, wired only to logging
    callbacks for now (PORTA/PORTB writes). It is NOT yet bit-level wired
    to the Z180's ASCI1 RX pin - that is the next RE step. The D017 tick
    stub (see d017_tick below) stays in place as the firmware-side
    fallback while that real serial link isn't connected.

***************************************************************************/

#include "emu.h"

#include "cpu/z180/z180.h"
#include "cpu/pic16c5x/pic16c5x.h"
#include "video/hd44780.h"

#include "emupal.h"
#include "machine/timer.h"
#include "screen.h"


namespace {

class writer_state : public driver_device
{
public:
	writer_state(const machine_config &mconfig, device_type type, const char *tag)
		: driver_device(mconfig, type, tag)
		, m_maincpu(*this, "maincpu")
		, m_piccpu(*this, "piccpu")
		, m_lcdc(*this, "ks0066_%u", 0U)
		, m_rom(*this, "maincpu")
		, m_kb(*this, "ROW%u", 0U)
	{ }

	void writer(machine_config &config);

protected:
	virtual void machine_start() override ATTR_COLD;

private:
	required_device<z8s180_device> m_maincpu;
	required_device<pic16c54_device> m_piccpu;
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
	TIMER_DEVICE_CALLBACK_MEMBER(d017_tick);
	virtual void machine_reset() override ATTR_COLD;

	// REAL PIC16C54C (U11). Its free-running loop toggles these ports
	// continuously (confirmed by the real-dump disassembly, see header
	// comment) - logging every write floods error.log (12000+ lines in a
	// few seconds) and chokes emulation speed for no new information, so
	// these are no-ops for now. Re-enable logerror here only for a PIC-
	// specific trace, not for general Z180/keyboard/LCD runs.
	uint8_t pic_porta_r() { return 0x00; } // TODO: real handshake lines, not wired yet

	// PIC pin1 (RA2) = Z180 pin11 (/INT1), per continuity measurement
	// (WRITER_RE_06_PIC_mapa_conexiones_U11, 2026-11-22, "Confirmado").
	// CONFIRMED BY DISASSEMBLY, NOT GUESSED (Ghidra analysis_20261121 +
	// MAME src/devices/cpu/z180/z180.cpp cross-check, 2026-11-23):
	//   - Reset code at ROM 0x242d-2431 sets IM 1 then LD I,A with A=0xd7.
	//   - ROM 0x2433-2438 clears ITC bits 0-2 (AND 0xf8) right after,
	//     then EI at 0x243b.
	//   - The very next call, ROM 0x214b (invoked once, at 0x243c, as
	//     part of boot), does IN0 A,(0x34) / OR 0x2 / OUT0 (0x34),A -
	//     this SETS ITC bit 1, i.e. Z180_ITC_ITE1 (=0x02 in z180.cpp) -
	//     so the firmware DOES explicitly enable the IRQ1 interrupt
	//     shortly after reset. /INT1 is a live, intended interrupt line,
	//     not a leftover/unused pin.
	// OPEN, STILL NOT RESOLVED (this is the real next step, not a guess
	// to fill in): per z180.cpp's take_interrupt(), IRQ1 does not use
	// RST 38h - it reads a 16-bit pointer from RAM at ((I<<8)|IL) and
	// jumps there. With I=0xd7 and IL defaulting to 0x00 (m_il never
	// written anywhere in the 64 KiB analyzed ROM window - no OUT0/IN0
	// on port 0x33 at all), that address is 0xD700. No instruction in
	// the analyzed window writes a literal 0xd6xx/0xd70x/0xd71x value
	// anywhere, so no confirmed handler pointer is installed there in
	// this window. Firing /INT1 may therefore jump through whatever
	// happens to be in RAM at 0xD700/0xD701 - which would explain erratic
	// parking rather than reaching a real handler. Two real possibilities,
	// neither assumed here: (a) the vector IS set up, but by code in a
	// banked ROM region outside Ghidra's 64 KiB logical window (CBAR/
	// CBR/BBR banking - not yet checked), or (b) IL is written via some
	// path not yet found and 0xD700 is the wrong address. Next step:
	// check the banked regions for a write to port 0x33 or to the
	// resulting vector address before drawing conclusions from this
	// wiring alone. Electrical polarity (asserted-low) is still per the
	// Z180 INT1 convention, not individually measured.
	// TEMPORARILY DISCONNECTED (2026-11-23, by explicit instruction, after
	// the full-ROM scan above found no handler installed at the computed
	// IRQ1 vector 0xD700 anywhere in the 1 MiB image - see
	// WRITER_RE_08_IRQ1_vector_Ghidra sec. 5). The electrical wiring
	// (RA2 -> /INT1) stays as documented above and is not in question;
	// only the line is not driven for now, to isolate whether the hang
	// comes from somewhere else. Re-enable with the commented-out line
	// below once a real IRQ1 handler/vector is located.
	void pic_porta_w(uint8_t data)
	{
		// m_maincpu->set_input_line(Z180_INPUT_LINE_IRQ1, BIT(data, 2) ? CLEAR_LINE : ASSERT_LINE);
	}
	void pic_portb_w(uint8_t data) { }

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
	logerror("LCDWR ctrl=%u rs=%u data=%02X ('%c') PC=%04X\n", ctrl, rs, data,
		(data >= 0x20 && data < 0x7f) ? data : '.', m_maincpu->pc());
	m_lcdc[ctrl]->write(rs, data);
}

// STUB, NOT a reconstruction of the real PIC protocol: the Z180 core in this
// MAME tree has no ASCI1 receive simulation at all (no code anywhere sets
// RDR1/STAT1's RDRF bit), and nothing increments the RAM variable at D017
// that routine 062F (read-ASCI1-byte-with-timeout) uses as its elapsed-time
// counter to give up and return 0 when no byte arrives. Confirmed by a final
// state dump: PC parks forever inside 062F's wait loop with D017 and D073
// (the generic interrupt-flag byte) stuck at their boot-time values. Without
// *some* source of elapsed time, every call to 062F after the first few
// (which apparently resolve before this timer starts mattering) hangs
// forever, and the firmware never reaches any text-printing code.
// This timer only advances that one RAM counter, at an arbitrary rate, so
// the firmware's own documented "nothing answered -> proceed with zeros"
// behavior (D011/D013 already 0000 at boot) can run its course. It does not
// supply any PIC byte value, timing, or protocol semantics.
TIMER_DEVICE_CALLBACK_MEMBER(writer_state::d017_tick)
{
	uint16_t v = m_mem[0xcd017] | (m_mem[0xcd018] << 8);
	v++;
	m_mem[0xcd017] = v & 0xff;
	m_mem[0xcd018] = v >> 8;
}

void writer_state::report_writes()
{
	logerror("MEMWR summary (4 KiB pages with writes):\n");
	for (int i = 0; i < 256; i++)
		if (m_wr_pages[i])
			logerror("  %05X-%05X : %u writes\n", i << 12, (i << 12) | 0xfff, m_wr_pages[i]);

	// Final CPU/RAM snapshot for RE: where did execution park, and what do the
	// boot-flow variables hold (D000/D002 RAM signatures, D011/D013 PIC bytes,
	// D0AD/D08D used by 0CBE, D073 tick flag set by the RST 38h ISR).
	uint16_t pc = m_maincpu->pc();
	uint16_t sp = m_maincpu->state_int(Z180_SP);
	auto rd16 = [this](uint32_t a) { return m_mem[a] | (m_mem[a + 1] << 8); };
	// logical Dxxx -> physical 0xC0000 + Dxxx (CBAR=F0/CBR=C0/BBR=C0 per RE notes)
	logerror("FINAL PC=%04X SP=%04X D000=%04X D002=%04X D011=%04X D013=%04X D0AD=%04X D073=%02X\n",
		pc, sp, rd16(0xcd000), rd16(0xcd002), rd16(0xcd011),
		rd16(0xcd013), rd16(0xcd0ad), m_mem[0xcd073]);
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

	// ANALYSIS-ONLY instrumentation: force a valid boot RAM signature
	// (D000=916Ah, D002=5A3Ch, logical Dxxx -> physical 0xC0000+Dxxx) so the
	// firmware skips "MEMORY ERROR WARNING" and reaches normal operation.
	// This is ONLY for keyboard-matrix mapping (watching which (row,col)
	// echoes which character on the live LCD). It must stay OFF (set this
	// to false and rebuild) for any run meant to document real physical
	// recovery, where reaching this screen genuinely is the starting state.
	constexpr bool ANALYSIS_FORCE_VALID_RAM_SIGNATURE = true;
	if (ANALYSIS_FORCE_VALID_RAM_SIGNATURE)
	{
		m_mem[0xcd000] = 0x6a; m_mem[0xcd001] = 0x91;
		m_mem[0xcd002] = 0x3c; m_mem[0xcd003] = 0x5a;
	}
}


// Matrix is active-high (pressed = 1). Undefined bits read 0.
// Full 8x16 matrix declared so every (row,col) position exists as a field
// (needed either way for Lua-driven automation later). Most positions are
// also bound to a distinct PC key so a human can press PC keys in the
// graphical window and watch the live LCD to map physical keycaps -
// this is the actual unknown (electrical wiring of the real keyboard),
// not something software analysis alone can supply.
// Pool exhausted before covering row6 cols 8-15 and all of row7 - those
// stay unbound (field exists, no PORT_CODE) in this pass.
static INPUT_PORTS_START( writer )
	PORT_START("ROW0")
	PORT_BIT(0x0001, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("R0C0") PORT_CODE(KEYCODE_A)
	PORT_BIT(0x0002, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("R0C1") PORT_CODE(KEYCODE_B)
	PORT_BIT(0x0004, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("R0C2") PORT_CODE(KEYCODE_C)
	PORT_BIT(0x0008, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("R0C3") PORT_CODE(KEYCODE_D)
	PORT_BIT(0x0010, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("R0C4") PORT_CODE(KEYCODE_E)
	PORT_BIT(0x0020, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("R0C5") PORT_CODE(KEYCODE_F)
	PORT_BIT(0x0040, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("R0C6") PORT_CODE(KEYCODE_G)
	PORT_BIT(0x0080, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("R0C7") PORT_CODE(KEYCODE_H)
	PORT_BIT(0x0100, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("R0C8") PORT_CODE(KEYCODE_I)
	PORT_BIT(0x0200, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("R0C9") PORT_CODE(KEYCODE_J)
	PORT_BIT(0x0400, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("R0C10") PORT_CODE(KEYCODE_K)
	PORT_BIT(0x0800, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("R0C11") PORT_CODE(KEYCODE_L)
	PORT_BIT(0x1000, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("R0C12") PORT_CODE(KEYCODE_M)
	PORT_BIT(0x2000, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("R0C13") PORT_CODE(KEYCODE_N)
	PORT_BIT(0x4000, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("R0C14") PORT_CODE(KEYCODE_O)
	PORT_BIT(0x8000, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("R0C15") PORT_CODE(KEYCODE_P)

	PORT_START("ROW1")
	PORT_BIT(0x0001, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("R1C0") PORT_CODE(KEYCODE_Q)
	PORT_BIT(0x0002, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("R1C1") PORT_CODE(KEYCODE_R)
	PORT_BIT(0x0004, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("R1C2") PORT_CODE(KEYCODE_S)
	PORT_BIT(0x0008, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("R1C3") PORT_CODE(KEYCODE_T)
	PORT_BIT(0x0010, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("R1C4") PORT_CODE(KEYCODE_U)
	PORT_BIT(0x0020, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("R1C5") PORT_CODE(KEYCODE_V)
	PORT_BIT(0x0040, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("R1C6") PORT_CODE(KEYCODE_W)
	PORT_BIT(0x0080, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("R1C7") PORT_CODE(KEYCODE_X)
	PORT_BIT(0x0100, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("R1C8") PORT_CODE(KEYCODE_Y)
	PORT_BIT(0x0200, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("R1C9") PORT_CODE(KEYCODE_Z)
	PORT_BIT(0x0400, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("R1C10") PORT_CODE(KEYCODE_0)
	PORT_BIT(0x0800, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("R1C11") PORT_CODE(KEYCODE_1)
	PORT_BIT(0x1000, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("R1C12") PORT_CODE(KEYCODE_2)
	PORT_BIT(0x2000, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("R1C13") PORT_CODE(KEYCODE_3)
	PORT_BIT(0x4000, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("R1C14") PORT_CODE(KEYCODE_4)
	PORT_BIT(0x8000, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("R1C15") PORT_CODE(KEYCODE_5)

	PORT_START("ROW2")
	PORT_BIT(0x0001, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("R2C0") PORT_CODE(KEYCODE_6)
	PORT_BIT(0x0002, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("R2C1") PORT_CODE(KEYCODE_7)
	PORT_BIT(0x0004, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("R2C2") PORT_CODE(KEYCODE_8)
	PORT_BIT(0x0008, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("R2C3") PORT_CODE(KEYCODE_9)
	PORT_BIT(0x0010, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("R2C4") PORT_CODE(KEYCODE_F4)
	PORT_BIT(0x0020, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("R2C5") PORT_CODE(KEYCODE_F5)
	PORT_BIT(0x0040, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("MR key 1 (R2C6)") PORT_CODE(KEYCODE_F1)
	PORT_BIT(0x0080, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("R2C7") PORT_CODE(KEYCODE_F6)
	PORT_BIT(0x0100, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("MR key 3 (R2C8)") PORT_CODE(KEYCODE_F3)
	PORT_BIT(0x0200, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("R2C9") PORT_CODE(KEYCODE_F7)
	PORT_BIT(0x0400, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("R2C10") PORT_CODE(KEYCODE_F8)
	PORT_BIT(0x0800, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("R2C11") PORT_CODE(KEYCODE_F9)
	PORT_BIT(0x1000, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("R2C12") PORT_CODE(KEYCODE_F10)
	PORT_BIT(0x2000, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("R2C13") PORT_CODE(KEYCODE_F11)
	PORT_BIT(0x4000, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("R2C14") PORT_CODE(KEYCODE_F12)
	PORT_BIT(0x8000, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("R2C15") PORT_CODE(KEYCODE_0_PAD)

	PORT_START("ROW3")
	PORT_BIT(0x0001, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("R3C0") PORT_CODE(KEYCODE_1_PAD)
	PORT_BIT(0x0002, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("R3C1") PORT_CODE(KEYCODE_2_PAD)
	PORT_BIT(0x0004, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("R3C2") PORT_CODE(KEYCODE_3_PAD)
	PORT_BIT(0x0008, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("R3C3") PORT_CODE(KEYCODE_4_PAD)
	PORT_BIT(0x0010, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("R3C4") PORT_CODE(KEYCODE_5_PAD)
	PORT_BIT(0x0020, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("R3C5") PORT_CODE(KEYCODE_6_PAD)
	PORT_BIT(0x0040, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("R3C6") PORT_CODE(KEYCODE_7_PAD)
	PORT_BIT(0x0080, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("R3C7") PORT_CODE(KEYCODE_8_PAD)
	PORT_BIT(0x0100, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("R3C8") PORT_CODE(KEYCODE_9_PAD)
	PORT_BIT(0x0200, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("R3C9") PORT_CODE(KEYCODE_DEL_PAD)
	PORT_BIT(0x0400, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("R3C10") PORT_CODE(KEYCODE_PLUS_PAD)
	PORT_BIT(0x0800, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("R3C11") PORT_CODE(KEYCODE_MINUS_PAD)
	PORT_BIT(0x1000, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("R3C12") PORT_CODE(KEYCODE_SLASH_PAD)
	PORT_BIT(0x2000, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("R3C13") PORT_CODE(KEYCODE_ASTERISK)
	PORT_BIT(0x4000, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("R3C14") PORT_CODE(KEYCODE_ENTER_PAD)
	PORT_BIT(0x8000, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("R3C15") PORT_CODE(KEYCODE_MINUS)

	PORT_START("ROW4")
	PORT_BIT(0x0001, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("R4C0") PORT_CODE(KEYCODE_EQUALS)
	PORT_BIT(0x0002, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("R4C1") PORT_CODE(KEYCODE_BACKSLASH)
	PORT_BIT(0x0004, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("R4C2") PORT_CODE(KEYCODE_OPENBRACE)
	PORT_BIT(0x0008, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("R4C3") PORT_CODE(KEYCODE_CLOSEBRACE)
	PORT_BIT(0x0010, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("R4C4") PORT_CODE(KEYCODE_COLON)
	PORT_BIT(0x0020, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("R4C5") PORT_CODE(KEYCODE_QUOTE)
	PORT_BIT(0x0040, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("R4C6") PORT_CODE(KEYCODE_COMMA)
	PORT_BIT(0x0080, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("R4C7") PORT_CODE(KEYCODE_STOP)
	PORT_BIT(0x0100, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("R4C8") PORT_CODE(KEYCODE_SLASH)
	PORT_BIT(0x0200, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("R4C9") PORT_CODE(KEYCODE_TILDE)
	PORT_BIT(0x0400, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("R4C10") PORT_CODE(KEYCODE_UP)
	PORT_BIT(0x0800, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("R4C11") PORT_CODE(KEYCODE_DOWN)
	PORT_BIT(0x1000, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("R4C12") PORT_CODE(KEYCODE_LEFT)
	PORT_BIT(0x2000, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("R4C13") PORT_CODE(KEYCODE_RIGHT)
	PORT_BIT(0x4000, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("R4C14") PORT_CODE(KEYCODE_HOME)
	PORT_BIT(0x8000, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("R4C15") PORT_CODE(KEYCODE_END)

	PORT_START("ROW5")
	PORT_BIT(0x0001, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("R5C0") PORT_CODE(KEYCODE_PGUP)
	PORT_BIT(0x0002, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("R5C1") PORT_CODE(KEYCODE_PGDN)
	PORT_BIT(0x0004, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("R5C2") PORT_CODE(KEYCODE_INSERT)
	PORT_BIT(0x0008, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("R5C3") PORT_CODE(KEYCODE_DEL)
	PORT_BIT(0x0010, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("R5C4") PORT_CODE(KEYCODE_ENTER)
	PORT_BIT(0x0020, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("R5C5") PORT_CODE(KEYCODE_SPACE)
	PORT_BIT(0x0040, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("R5C6") PORT_CODE(KEYCODE_TAB)
	PORT_BIT(0x0080, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("R5C7") PORT_CODE(KEYCODE_BACKSPACE)
	PORT_BIT(0x0100, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("R5C8") PORT_CODE(KEYCODE_ESC)
	PORT_BIT(0x0200, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("R5C9") PORT_CODE(KEYCODE_LSHIFT)
	PORT_BIT(0x0400, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("R5C10") PORT_CODE(KEYCODE_RSHIFT)
	PORT_BIT(0x0800, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("R5C11") PORT_CODE(KEYCODE_LCONTROL)
	PORT_BIT(0x1000, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("R5C12") PORT_CODE(KEYCODE_RCONTROL)
	PORT_BIT(0x2000, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("R5C13") PORT_CODE(KEYCODE_LALT)
	PORT_BIT(0x4000, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("R5C14") PORT_CODE(KEYCODE_RALT)
	PORT_BIT(0x8000, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("R5C15") PORT_CODE(KEYCODE_LWIN)

	PORT_START("ROW6")
	PORT_BIT(0x0001, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("R6C0") PORT_CODE(KEYCODE_RWIN)
	PORT_BIT(0x0002, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("R6C1") PORT_CODE(KEYCODE_CAPSLOCK)
	PORT_BIT(0x0004, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("R6C2") PORT_CODE(KEYCODE_NUMLOCK)
	PORT_BIT(0x0008, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("R6C3") PORT_CODE(KEYCODE_SCRLOCK)
	PORT_BIT(0x0010, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("R6C4") PORT_CODE(KEYCODE_PRTSCR)
	PORT_BIT(0x0020, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("R6C5") PORT_CODE(KEYCODE_PAUSE)
	PORT_BIT(0x0040, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("MR key 2 (R6C6)") PORT_CODE(KEYCODE_F2)
	PORT_BIT(0x0080, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("R6C7") PORT_CODE(KEYCODE_MENU)
	PORT_BIT(0x0100, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("R6C8 (sin tecla asignada)")
	PORT_BIT(0x0200, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("R6C9 (sin tecla asignada)")
	PORT_BIT(0x0400, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("R6C10 (sin tecla asignada)")
	PORT_BIT(0x0800, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("R6C11 (sin tecla asignada)")
	PORT_BIT(0x1000, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("R6C12 (sin tecla asignada)")
	PORT_BIT(0x2000, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("R6C13 (sin tecla asignada)")
	PORT_BIT(0x4000, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("R6C14 (sin tecla asignada)")
	PORT_BIT(0x8000, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("R6C15 (sin tecla asignada)")

	PORT_START("ROW7")
	PORT_BIT(0x0001, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("R7C0 (sin tecla asignada)")
	PORT_BIT(0x0002, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("R7C1 (sin tecla asignada)")
	PORT_BIT(0x0004, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("R7C2 (sin tecla asignada)")
	PORT_BIT(0x0008, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("R7C3 (sin tecla asignada)")
	PORT_BIT(0x0010, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("R7C4 (sin tecla asignada)")
	PORT_BIT(0x0020, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("R7C5 (sin tecla asignada)")
	PORT_BIT(0x0040, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("R7C6 (sin tecla asignada)")
	PORT_BIT(0x0080, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("R7C7 (sin tecla asignada)")
	PORT_BIT(0x0100, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("R7C8 (sin tecla asignada)")
	PORT_BIT(0x0200, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("R7C9 (sin tecla asignada)")
	PORT_BIT(0x0400, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("R7C10 (sin tecla asignada)")
	PORT_BIT(0x0800, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("R7C11 (sin tecla asignada)")
	PORT_BIT(0x1000, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("R7C12 (sin tecla asignada)")
	PORT_BIT(0x2000, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("R7C13 (sin tecla asignada)")
	PORT_BIT(0x4000, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("R7C14 (sin tecla asignada)")
	PORT_BIT(0x8000, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("R7C15 (sin tecla asignada)")
INPUT_PORTS_END


void writer_state::writer(machine_config &config)
{
	Z8S180(config, m_maincpu, 6'144'000); // crystal Y2 ECS 6.144 MHz; CCR/CMR set by firmware
	m_maincpu->set_addrmap(AS_PROGRAM, &writer_state::map_program);
	m_maincpu->set_addrmap(AS_IO, &writer_state::map_io);

	// STUB tick for D017 (see d017_tick comment above) - arbitrary rate,
	// just fast enough that 062F's timeout resolves promptly in our runs.
	TIMER(config, "d017_tick").configure_periodic(FUNC(writer_state::d017_tick), attotime::from_hz(1000));

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

	// REAL PIC16C54C (U11), crystal Y1 32.768 kHz (confirmed: CONFIG FOSC=LP
	// in the real dump). RE-only wiring for now - see header comment and
	// pic_porta_r/pic_porta_w/pic_portb_w above: not yet bit-level linked to
	// the Z180's ASCI1 RX line.
	PIC16C54(config, m_piccpu, 32'768);
	m_piccpu->set_config(0xffc); // real CONFIG word from the physical dump: CP=1, WDTE=1, FOSC=LP
	m_piccpu->read_a().set(FUNC(writer_state::pic_porta_r));
	m_piccpu->write_a().set(FUNC(writer_state::pic_porta_w));
	m_piccpu->write_b().set(FUNC(writer_state::pic_portb_w));
}


ROM_START( akwriter )
	ROM_REGION( 0x100000, "maincpu", 0 ) // physical region read by the shadow; no direct ROM mapping in diagnostic build
	ROM_LOAD( "akt_writer_v3.70_m27c801.bin", 0x000000, 0x100000, CRC(c454875c) SHA1(6bd0691f96c7987b2e266e220834bf4d7e3e22b4) )

	// REAL PIC16C54C (U11) dump: Arduino+Willem reader, DS30190 verify-mode,
	// 3 identical reads (SHA256 verified separately). 512 words of 12 bits,
	// stored as 16-bit little-endian words (standard MAME PIC16C5x format).
	ROM_REGION( 0x400, "piccpu", 0 )
	ROM_LOAD( "writer_instructor_pic16c54c_u11.bin", 0x000, 0x400, CRC(d7690376) SHA1(2259c2c463d35730ea2b80caa8bf970d677ca287) )
ROM_END

} // anonymous namespace


//    YEAR  NAME      PARENT  COMPAT  MACHINE  INPUT   CLASS         INIT        COMPANY     FULLNAME                       FLAGS
COMP( 2005, akwriter, 0,      0,      writer,  writer, writer_state, empty_init, "AKT Inc.", "The Writer (v3.70)", MACHINE_NOT_WORKING | MACHINE_NO_SOUND )
