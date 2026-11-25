// license:BSD-3-Clause
// copyright-holders:Sandro Ronco, Bartman/Abyss, SamDelorean
/***************************************************************************

    The Writer — AKT Writer Instructor, firmware v3.70

    Experimental MAME driver. MACHINE_NOT_WORKING remains intentional:
    the CPU now reaches the firmware's memory-warning idle state, but the
    physical SRAM map, power/reset controls and full keyboard behavior are
    not yet reproduced.

    Hardware identified:
      - Zilog Z8S180, 6.144 MHz
      - M27C801 EPROM, 1 MiB
      - 256 KiB SRAM (two devices; chip-select topology still being mapped)
      - 40x4 character LCD, two KS0066 controllers
      - PIC16C54C U11, 32.768 kHz, real 1 KiB firmware dump
      - 8x16 keyboard matrix, two 74VHC244 buffers and 74HC138 decoder

    Verified boot-path integration (2026-11-25):
      - PIC RA2 -> Z180 /INT1; firmware installs IRQ1 vector 0x182A at D700
      - PIC RB0 -> Z180 ASCI1 RXA1; Z180 TXA1 is sampled on PIC RB7
      - PIC RB1 samples the Z180 /HALT state, including SLP
      - PIC RB2 uses an idle-high J1 default (physical idle level unmeasured)
      - PIC RB3 currently models aggregate keyboard activity; exact U9 2A1
        matrix-column mapping is still pending
      - synthetic D017 1 kHz tick is disabled by default; opt-in only via
        WRITER_ENABLE_D017_TICK=1

    A headless run now reaches the firmware's MEMORY ERROR WARNING and
    sleeps at PC 0x0E27. This is not a CPU deadlock: the cold SRAM signature
    is invalid. A real SRAM image or the documented master-reset procedure
    is needed to reach a normal initialized editor state.

    Remaining fidelity gaps include PIC RA1 -> Z180 /RESET, PIC RA3 -> SRAM
    CS2, PIC T0CKI <- U19 /Y3, RB4/RB5/RB6, the exact RB3 keyboard column,
    a physical 256 KiB SRAM map/persistence, and full keycap mapping.

    ROM data is not distributed with this source; use a dump from the
    owner's device. See README.md and docs/BOOT_BLOCKER_RESOLUTION_20261125.md.

***************************************************************************/

#include "emu.h"

#include <cstdio>
#include <cstdlib>

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
		, m_mainbank(*this, "mainbank")
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
	required_memory_bank m_mainbank;
	required_ioport_array<8> m_kb;
	std::unique_ptr<uint8_t[]> m_mem;
	uint8_t m_kb_strobe = 0;
	int m_pic_ra2_last = -1;
	int m_pic_rb0_last = -1;
	int m_pic_portb_last = -1;
	int m_z180_txa1 = 1;
	int m_pic_force_wake_reads = 0;
	bool m_pic_rb2_idle_high = true;
	bool m_test_master_reset_keys = false;
	int m_test_master_reset_scan_reads = 0;
	bool m_test_bypass_password_to_reset = false;
	bool m_test_redirect_irq1_to_reset = false;
	uint16_t m_test_saved_irq1_vector = 0;
	bool m_trace_input_writes = false;
	unsigned m_trace_input_write_count = 0;
	bool m_trace_keyboard = false;
	unsigned m_trace_keyboard_count = 0;
	bool m_trace_input_reads = false;
	unsigned m_trace_input_read_count = 0;
	bool m_trace_password_flow = false;
	uint16_t m_trace_password_last_pc = 0xffff;
	unsigned m_trace_password_count = 0;
	emu_timer *m_test_master_reset_timer = nullptr;

	uint16_t kb_sense();
	bool any_key_pressed();
	bool z180_halt_pin_asserted() const;
	uint8_t pic_portb_r();
	void z180_txa1_w(int state) { m_z180_txa1 = state; }
	void kb_strobe_w(uint8_t data);
	uint8_t kb_sense_lo_r() { return kb_sense() & 0xff; }
	uint8_t kb_sense_hi_r() { return kb_sense() >> 8; }
	uint8_t ext_io_r(offs_t offset);
	void ext_io_w(offs_t offset, uint8_t data);
	uint8_t lcd_r(offs_t offset);
	void lcd_w(offs_t offset, uint8_t data);
	void report_writes();
	TIMER_DEVICE_CALLBACK_MEMBER(d017_tick);
	TIMER_CALLBACK_MEMBER(test_master_reset);
	virtual void machine_reset() override ATTR_COLD;

	// Minimal headless frame-dump bridge, same env-var pattern as AS2K's
	// alphasma.cpp (AS2K_UI_FRAME_FILE), ported here for writerdiag only:
	// this build is the diagnostic target, so this kind of instrumentation
	// is the point of it. Opt-in only (env var unset = zero overhead, no
	// behavior change for normal headless self-test runs). Dumps the
	// already-rendered LCD bitmap as a binary PPM every few frames so a
	// screenshot can be pulled without SDL/X11 window automation tools.
	std::string m_ui_frame_path;
	uint64_t m_ui_frame_counter = 0;
	void ui_frame_dump(screen_device &screen, bitmap_ind16 &bitmap);

	// PIC RA4/T0CKI is a separate external input from U19 /Y3. Its decoder
	// source has not been verified, so leave the RTCC input unclocked for now.
	uint8_t pic_porta_r() { return 0x00; }

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
	// IRQ1 vector is installed by the firmware itself; the earlier RE08
	// conclusion that D700 was uninitialized missed this indirect setup:
	//   0x214B: LD HL,0 / LD DE,0x182A / CALL 0x040C
	//   0x040C: LD A,I / LD D,A / LD E,0 / ADD HL,DE / LD (HL),E /
	//          INC HL / LD (HL),D
	// With I=0xD7 this writes the 16-bit pointer 0x182A at D700-D701.
	// All four RAM snapshots confirm [D700]=2A 18. The ISR at 0x182A
	// updates D015/D017/D00F/D011/D013 and returns via EI/RET.
	// The Z180 reaches SLP at 0x0E27 while waiting for this interrupt.
	// PIC disassembly shows RA2 pulses low/high at words 0x0C1-0x0C2;
	// board continuity confirms RA2 -> active-low Z180 /INT1. Therefore
	// drive IRQ1 from RA2. RB0 -> RXA1 is also wired in pic_portb_w().
	void pic_porta_w(uint8_t data)
	{
		const int ra2_high = BIT(data, 2);
		m_maincpu->set_input_line(Z180_INPUT_LINE_IRQ1, ra2_high ? CLEAR_LINE : ASSERT_LINE);
		if (ra2_high != m_pic_ra2_last) {
			logerror("PIC RA2=%d -> Z180 /INT1 %s\n", ra2_high, ra2_high ? "CLEAR" : "ASSERT");
			m_pic_ra2_last = ra2_high;
		}
	}
	void pic_portb_w(uint8_t data)
	{
		// PIC RB0 (U11 pin 6) is physically connected to Z180 RXA1 (pin 53).
		const int rb0 = BIT(data, 0);
		m_maincpu->rxa1_w(rb0);
		if (rb0 != m_pic_rb0_last) {
			logerror("PIC RB0/TX=%d -> Z180 RXA1\n", rb0);
			m_pic_rb0_last = rb0;
		}
	}

	void map_program(address_map &map) ATTR_COLD;
	void map_io(address_map &map) ATTR_COLD;
	void palette_init(palette_device &palette) const;
	uint32_t screen_update(screen_device &screen, bitmap_ind16 &bitmap, const rectangle &cliprect);
};


void writer_state::map_program(address_map &map)
{
	// Native banked RAM/ROM shadow (same fast-path technique as AS2K's
	// alphasma.cpp: map(...).bankrw(...)/rom().region(...), not a per-byte
	// .rw() handler). The earlier version routed the entire 1 MiB through
	// mem_r/mem_w for per-page "first write" logging; that put every single
	// opcode fetch and operand access through MAME's address_map device
	// dispatch, which measured far slower here than AS2K's equivalent diag
	// build using the same compiler flags (OPTIMIZE=2 SSE2=0, confirmed
	// identical between build_writerdiag.sh and build-as2k-stable-arm64.sh).
	// Exhaustive per-access logging already served its purpose (the MEMWR
	// page-write pattern that led to the self-relocation finding is
	// documented in Drive); ongoing inspection now uses the WRITER_MEM_DUMP_FILE
	// one-shot snapshot and WRITER_UI_FRAME_FILE periodic frame dump instead,
	// neither of which costs anything on the hot path.
	map(0x00000, 0xfffff).bankrw(m_mainbank);
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

	// Test-only cold-boot chord injection. The firmware's 0x23AC matrix
	// signature is F1=R2C6 plus F2=R6C6. Inject exactly one complete scan
	// during its early 0x0CF6 check; never alter normal keyboard reads.
	const uint16_t pc = m_maincpu->pc();
	if (m_test_master_reset_keys && m_test_master_reset_scan_reads < 16 && pc >= 0x0d00 && pc <= 0x0d20)
	{
		if (m_kb_strobe == 0x20 || m_kb_strobe == 0x02)
			v |= 0x0040;
		logerror("TEST MASTER RESET KEY scan=%d strobe=%02X value=%04X PC=%04X\n",
			++m_test_master_reset_scan_reads, m_kb_strobe, v, pc);
		if (m_test_master_reset_scan_reads == 16)
		{
			m_test_master_reset_keys = false;
			logerror("TEST MASTER RESET KEY chord released\n");
		}
	}
	if (m_trace_keyboard && m_trace_keyboard_count < 4096)
	{
		logerror("KBREAD strobe=%02X value=%04X PC_NEXT=%04X\n", m_kb_strobe, v, m_maincpu->pc());
		++m_trace_keyboard_count;
	}
	return v;
}

void writer_state::kb_strobe_w(uint8_t data)
{
	m_kb_strobe = data;
	if (m_trace_keyboard && m_trace_keyboard_count < 4096)
		logerror("KBSTROBE value=%02X PC_NEXT=%04X\n", data, m_maincpu->pc());
}

bool writer_state::any_key_pressed()
{
	// The PIC sees one keyboard-buffer input at U9 pin 11 (2A1). Until the
	// exact column index is confirmed, aggregate the active-high matrix as
	// a wake request. The Z180 still performs the real 8x16 scan afterward.
	for (int row = 0; row < 8; row++)
		if (m_kb[row]->read() != 0)
			return true;
	return false;
}

bool writer_state::z180_halt_pin_asserted() const
{
	// MAME stores 1=HALT and 2=SLP in m_HALT, but its public Z180_HALT
	// state entry is masked to bit 0. Read the registered raw state byte so
	// the PIC's RB1 sees /HALT asserted for BOTH HALT and SLP.
	const device_state_entry *entry = m_maincpu->state_find_entry(Z180_HALT);
	return entry && (*static_cast<const uint8_t *>(entry->dataptr()) != 0);
}

uint8_t writer_state::pic_portb_r()
{
	uint8_t data = 0;
	// RB1 <- Z180 /HALT (active low); RB2 <- J1 serial input (idle-high
	// default, override with WRITER_PIC_RB2=0); RB3 <- keyboard wake;
	// RB7 <- Z180 TXA1. RB2's physical idle level is not yet measured.
	if (!z180_halt_pin_asserted()) data |= 0x02;
	if (m_pic_rb2_idle_high) data |= 0x04;
	const bool force_wake = (m_pic_force_wake_reads > 0);
	if (force_wake) --m_pic_force_wake_reads;
	if (force_wake || any_key_pressed()) data |= 0x08;
	if (m_z180_txa1) data |= 0x80;
	if (data != m_pic_portb_last) {
		logerror("PIC PORTB inputs=%02X (RB1=/HALT,RB2=J1,RB3=KEY,RB7=TXA1)\n", data);
		m_pic_portb_last = data;
	}
	return data;
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
// D017 is the firmware's 16-bit tick counter, normally advanced by the
// real PIC-driven IRQ1 handler at 0x182A. This synthetic 1 kHz timer is a
// diagnostic fallback from the earlier disconnected-PIC stage; it is not
// enabled by default and is available only with WRITER_ENABLE_D017_TICK=1.
// It supplies no PIC bytes or protocol semantics.
TIMER_DEVICE_CALLBACK_MEMBER(writer_state::d017_tick)
{
	uint16_t v = m_mem[0xcd017] | (m_mem[0xcd018] << 8);
	v++;
	m_mem[0xcd017] = v & 0xff;
	m_mem[0xcd018] = v >> 8;
}
// Test-only entry into the ROM's own Master Reset routine. Preserve the
// current stack so the initial CALL at 0x2523 can return normally; the ROM
// itself switches to its D000 stack at 0x2526.
TIMER_CALLBACK_MEMBER(writer_state::test_master_reset)
{
	logerror("TEST: redirecting Z180 PC to ROM Master Reset routine 2523 (preserving SP=%04X for its initial CALL 22F3)\n", m_maincpu->state_int(Z180_SP));
	m_maincpu->set_pc(0x2523);
}

void writer_state::report_writes()
{
	// One-shot live-RAM snapshot for Ghidra, gated by env var (same opt-in
	// pattern as WRITER_UI_FRAME_FILE). Needed because, per RE notes, only
	// the first 0x3000 bytes of physical C0000-CFFFF are a straight copy of
	// ROM bank 0x00 (the self-relocating boot LDIR); everything the CPU
	// writes afterward (memtest, variables, any code it builds at runtime)
	// is live RAM state that does not exist anywhere in the static ROM
	// file, under any bank. A flat ROM-bank import can never represent it;
	// this dumps what the CPU actually has at machine exit instead. Default
	// range covers logical 0000-FFFF = physical C0000-CFFFF (current
	// CBAR=F0/CBR=C0/BBR=C0 mapping), overridable via WRITER_MEM_DUMP_BASE/
	// WRITER_MEM_DUMP_LEN (hex, no 0x prefix) for other physical windows.
	if (char const *path = std::getenv("WRITER_MEM_DUMP_FILE"))
	{
		uint32_t base = 0xc0000;
		uint32_t len = 0x10000;
		if (char const *b = std::getenv("WRITER_MEM_DUMP_BASE"))
			base = strtoul(b, nullptr, 16);
		if (char const *l = std::getenv("WRITER_MEM_DUMP_LEN"))
			len = strtoul(l, nullptr, 16);
		if (base + len > 0x100000)
			len = 0x100000 - base;
		std::string tmp = std::string(path) + ".tmp";
		if (FILE *f = fopen(tmp.c_str(), "wb"))
		{
			fwrite(&m_mem[base], 1, len, f);
			fclose(f);
			std::rename(tmp.c_str(), path);
			logerror("MEMDUMP wrote %06X-%06X (%u bytes) to %s\n", base, base + len - 1, len, path);
		}
		else
		{
			logerror("MEMDUMP failed to open %s\n", tmp.c_str());
		}
	}

	// Per-page "first write" counting (m_wr_pages) was removed along with
	// mem_r/mem_w: it required routing the whole 1 MiB through a per-byte
	// .rw() handler instead of AS2K's native .bankrw()/.rom() fast path,
	// which measured far slower on this machine. It already served its
	// purpose (documented in Drive: the self-relocation LDIR into C0000-
	// C2FFF, and heavy memtest traffic in CC000-CDFFF). WRITER_MEM_DUMP_FILE
	// above gives an equivalent live-RAM view without that per-access cost.

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

	if (!m_ui_frame_path.empty() && (++m_ui_frame_counter % 15) == 0)
		ui_frame_dump(screen, bitmap);

	// One-shot diagnostic bypass: wait until the ROM has initialized its
	// six-character password editor, then temporarily redirect the real IRQ1
	// vector from 182A to 2523. The PIC's next real pulse wakes SLP and vectors
	// the CPU into the ROM reset routine; restore 182A when ROM writes its
	// D000/D002 initialization signatures.
	if (m_test_bypass_password_to_reset)
	{
		uint16_t const pc = m_maincpu->pc();
		uint16_t const sp = m_maincpu->state_int(Z180_SP);
		auto rd16 = [this](uint32_t a) { return uint16_t(m_mem[a] | (m_mem[a + 1] << 8)); };
		uint16_t const editor_buffer = rd16(0xcd1e7);
		uint16_t const editor_cursor = rd16(0xcd1e9);
		uint16_t const editor_length = rd16(0xcd1eb);
		uint16_t const editor_mode = rd16(0xcd1ed);
		bool const editor_ready = editor_buffer == 0xd03c && editor_cursor == 0xd03c &&
			editor_length == 6 && editor_mode == 0x008c;
		bool const editor_waiting = (pc == 0x0e27 || pc == 0x0e29);
		if (editor_ready && editor_waiting)
		{
			uint16_t const vector = uint16_t(m_mem[0xcd700] | (m_mem[0xcd701] << 8));
			if (vector == 0x182a)
			{
				m_test_saved_irq1_vector = vector;
				m_mem[0xcd700] = 0x23;
				m_mem[0xcd701] = 0x25;
				m_test_redirect_irq1_to_reset = true;
				logerror("TEST: password editor active (PC=%04X SP=%04X buffer=%04X cursor=%04X length=%u mode=%04X); redirected IRQ1 vector D700 %04X->2523; waiting for real PIC pulse to wake SLP\n", pc, sp, editor_buffer, editor_cursor, editor_length, editor_mode, vector);
			}
			else
				logerror("TEST: password editor active but IRQ1 vector D700=%04X, expected 182A; diagnostic redirect not armed\n", vector);
			m_test_bypass_password_to_reset = false;
		}
	}

	// The ROM writes these signatures at 0x254E/0x2554 after completing
	// initialization. Restore the normal interrupt handler before normal use.
	if (m_test_redirect_irq1_to_reset &&
		uint16_t(m_mem[0xcd000] | (m_mem[0xcd001] << 8)) == 0x916a &&
		uint16_t(m_mem[0xcd002] | (m_mem[0xcd003] << 8)) == 0x5a3c)
	{
		if (uint16_t(m_mem[0xcd700] | (m_mem[0xcd701] << 8)) == 0x2523)
		{
			m_mem[0xcd700] = uint8_t(m_test_saved_irq1_vector);
			m_mem[0xcd701] = uint8_t(m_test_saved_irq1_vector >> 8);
		}
		m_test_redirect_irq1_to_reset = false;
		logerror("TEST: ROM reset signature D000=916A/D002=5A3C detected; restored IRQ1 vector %04X\n", m_test_saved_irq1_vector);
	}

	if (m_trace_password_flow && m_trace_password_count < 4096)
	{
		uint16_t const pc = m_maincpu->pc();
		if (pc != m_trace_password_last_pc)
		{
			uint16_t const sp = m_maincpu->state_int(Z180_SP);
			logerror("PASSPC pc=%04X sp=%04X iff1=%d iff2=%d d0ad=%04X d0af=%04X d011=%04X d017=%04X d1e7=%04X d1e9=%04X d1eb=%04X\n",
				pc, sp, int(m_maincpu->state_int(Z180_IFF1)), int(m_maincpu->state_int(Z180_IFF2)),
				unsigned(m_mem[0xcd0ad] | (m_mem[0xcd0ae] << 8)),
				unsigned(m_mem[0xcd0af] | (m_mem[0xcd0b0] << 8)),
				unsigned(m_mem[0xcd011] | (m_mem[0xcd012] << 8)),
				unsigned(m_mem[0xcd017] | (m_mem[0xcd018] << 8)),
				unsigned(m_mem[0xcd1e7] | (m_mem[0xcd1e8] << 8)),
				unsigned(m_mem[0xcd1e9] | (m_mem[0xcd1ea] << 8)),
				unsigned(m_mem[0xcd1eb] | (m_mem[0xcd1ec] << 8)));
			m_trace_password_last_pc = pc;
			++m_trace_password_count;
		}
	}

	return 0;
}

void writer_state::ui_frame_dump(screen_device &screen, bitmap_ind16 &bitmap)
{
	// Same purpose as AS2K's AS2K_UI_FRAME_FILE in alphasma.cpp: dump the
	// already-rendered frame to a file so it can be pulled without any
	// SDL/X11 window automation tool, including fully headless
	// (-video none) runs where there is no window to screenshot at all.
	// Written to a .tmp path then renamed, so a reader never sees a
	// partially-written file.
	int const w = bitmap.width(), h = bitmap.height();
	std::string tmp_path = m_ui_frame_path + ".tmp";
	FILE *f = fopen(tmp_path.c_str(), "wb");
	if (!f)
		return;
	fprintf(f, "P6\n%d %d\n255\n", w, h);
	device_palette_interface &pal = screen.palette();
	for (int y = 0; y < h; y++)
	{
		uint16_t const *row = &bitmap.pix(y, 0);
		for (int x = 0; x < w; x++)
		{
			rgb_t const c = pal.pen_color(row[x]);
			uint8_t const rgb[3] = { c.r(), c.g(), c.b() };
			fwrite(rgb, 1, 3, f);
		}
	}
	fclose(f);
	std::rename(tmp_path.c_str(), m_ui_frame_path.c_str());
}

void writer_state::machine_start()
{
	m_tmp_bitmap = std::make_unique<bitmap_ind16>(6 * 40, 9 * 4);
	m_mem = std::make_unique<uint8_t[]>(0x100000);
	m_mainbank->set_base(&m_mem[0]);
	m_maincpu->rxa1_w(1); // UART idle state
	if (char const *force = std::getenv("WRITER_TEST_PIC_WAKE")) {
		if (force[0] == '1') m_pic_force_wake_reads = 64;
	}
	if (char const *rb2 = std::getenv("WRITER_PIC_RB2"))
		m_pic_rb2_idle_high = (rb2[0] != '0');
	if (char const *mr = std::getenv("WRITER_TEST_MASTER_RESET_KEYS"))
		m_test_master_reset_keys = (mr[0] == '1');
	if (char const *bypass = std::getenv("WRITER_TEST_BYPASS_PASSWORD_TO_RESET"))
		m_test_bypass_password_to_reset = (bypass[0] == '1');
	if (m_test_bypass_password_to_reset)
		logerror("TEST: one-shot password-screen bypass armed; will redirect the real IRQ1 vector after editor init, then restore it after ROM reset signatures\n");
	if (m_test_master_reset_keys)
		logerror("TEST: inject F1+F2 for the first firmware keyboard matrix scan\n");
	if (char const *trace = std::getenv("WRITER_TRACE_INPUT_WRITES"))
		m_trace_input_writes = (trace[0] == '1');
	if (char const *trace = std::getenv("WRITER_TRACE_KEYBOARD"))
		m_trace_keyboard = (trace[0] == '1');
	if (char const *trace = std::getenv("WRITER_TRACE_INPUT_READS"))
		m_trace_input_reads = (trace[0] == '1');
	if (char const *trace = std::getenv("WRITER_TRACE_PASSWORD_FLOW"))
		m_trace_password_flow = (trace[0] == '1');
	if (m_trace_password_flow)
		logerror("PASSWORD FLOW TRACE enabled: sampled PC/SP/IFF and editor/FIFO state once per frame; cap=4096\n");
	if (m_trace_input_writes)
	{
		// Passive Z180 program-space watchpoints. The callback observes the
		// write data and CPU state without replacing or modifying the write.
		auto &space = m_maincpu->space(AS_PROGRAM);
		space.install_write_tap(0x0cd03c, 0x0cd041, "writer_password_buffer",
			[this] (offs_t offset, u8 &data, u8) {
				if (m_trace_input_write_count++ < 4096)
					logerror("INWRITE BUF addr=%05X data=%02X PC_NEXT=%04X HL=%04X DE=%04X BC=%04X IFF1=%d IFF2=%d\n",
						unsigned(offset), data, m_maincpu->pc(),
						unsigned(m_maincpu->state_int(Z180_HL)),
						unsigned(m_maincpu->state_int(Z180_DE)),
						unsigned(m_maincpu->state_int(Z180_BC)),
						int(m_maincpu->state_int(Z180_IFF1)), int(m_maincpu->state_int(Z180_IFF2)));
			});
		space.install_write_tap(0x0cd08d, 0x0cd0af, "writer_event_queue",
			[this] (offs_t offset, u8 &data, u8) {
				if (m_trace_input_write_count++ < 4096)
					logerror("INWRITE QUEUE addr=%05X data=%02X PC_NEXT=%04X HL=%04X DE=%04X BC=%04X IFF1=%d IFF2=%d\n",
						unsigned(offset), data, m_maincpu->pc(),
						unsigned(m_maincpu->state_int(Z180_HL)),
						unsigned(m_maincpu->state_int(Z180_DE)),
						unsigned(m_maincpu->state_int(Z180_BC)),
						int(m_maincpu->state_int(Z180_IFF1)), int(m_maincpu->state_int(Z180_IFF2)));
			});
		logerror("INPUT WRITE TRACE enabled: CD03C-CD041 and CD08D-CD0AF (physical window for logical D03C/D08D); cap=4096\n");
	}
	if (m_trace_input_reads)
	{
		auto &space = m_maincpu->space(AS_PROGRAM);
		for (offs_t address : { offs_t(0x0cd0ad), offs_t(0x0cd0ae), offs_t(0x0cd0af), offs_t(0x0cd0b0) })
			space.install_read_tap(address, address, "writer_input_state_read",
				[this] (offs_t offset, u8 &data, u8) {
					if (m_trace_input_read_count++ < 4096)
						logerror("INREAD STATE addr=%05X data=%02X PC_NEXT=%04X IFF1=%d IFF2=%d\n",
							unsigned(offset), data, m_maincpu->pc(),
							int(m_maincpu->state_int(Z180_IFF1)), int(m_maincpu->state_int(Z180_IFF2)));
				});
		logerror("INPUT READ TRACE enabled: D0AD/D0AE queue count and D0AF/D0B0 pending state; cap=4096\n");
	}
	if (m_pic_force_wake_reads)
		logerror("TEST: one-shot PIC RB3 keyboard-wake pulse (%d PORTB reads)\n", m_pic_force_wake_reads);
	if (char const *mr = std::getenv("WRITER_TEST_EXECUTE_MASTER_RESET"))
	{
		if (mr[0] == '1')
		{
			m_test_master_reset_timer = timer_alloc(FUNC(writer_state::test_master_reset), this);
			m_test_master_reset_timer->adjust(attotime::from_seconds(2));
			logerror("TEST: schedule original ROM Master Reset routine after 2 emulated seconds\n");
		}
	}
	machine().add_notifier(MACHINE_NOTIFY_EXIT, machine_notify_delegate(&writer_state::report_writes, this));

	if (char const *path = std::getenv("WRITER_UI_FRAME_FILE"))
		m_ui_frame_path = path;
}

void writer_state::machine_reset()
{
	std::copy_n(&m_rom[0], 0x100000, &m_mem[0]);

	// Diagnostic-only bypass of the cold-SRAM signature gate. This does
	// NOT initialize the rest of SRAM and must never be enabled for normal
	// operation. It distinguishes a missing Master Reset signature from
	// later boot/model faults. Real Master Reset writes these same words.
	if (char const *force = std::getenv("WRITER_TEST_VALID_RAM_SIGNATURE"))
	{
		if (force[0] == '1')
		{
			m_mem[0xcd000] = 0x6a; m_mem[0xcd001] = 0x91;
			m_mem[0xcd002] = 0x3c; m_mem[0xcd003] = 0x5a;
			logerror("TEST: injected Master Reset SRAM signature at D000/D002\n");
		}
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
	m_maincpu->txa1_wr_callback().set(FUNC(writer_state::z180_txa1_w));

	// Real IRQ1 now advances D017. Keep the old synthetic tick opt-in only.
	if (std::getenv("WRITER_ENABLE_D017_TICK"))
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

	// REAL PIC16C54C (U11), crystal Y1 32.768 kHz (CONFIG FOSC=LP).
	// RA2 -> /INT1, RB0 -> RXA1, RB1 <- /HALT, RB7 <- TXA1 and RB2/RB3
	// inputs are wired in the callbacks above; T0CKI /Y3 remains pending.
	PIC16C54(config, m_piccpu, 32'768);
	m_piccpu->set_config(0xffc); // real CONFIG word from the physical dump: CP=1, WDTE=1, FOSC=LP
	m_piccpu->read_a().set(FUNC(writer_state::pic_porta_r));
	m_piccpu->read_b().set(FUNC(writer_state::pic_portb_r));
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
