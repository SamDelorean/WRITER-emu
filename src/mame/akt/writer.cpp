// license:BSD-3-Clause
// copyright-holders:Sandro Ronco, Bartman/Abyss, SamDelorean
/***************************************************************************

    AKT Inc. "The Writer" (Writer Instructor), firmware v3.70

    Minimal skeleton driver.

    Based on existing MAME drivers (BSD-3-Clause):
      - src/mame/brother/ax145.cpp      : Z180 family CPU + HD44780 word processor structure
      - src/mame/skeleton/alphasma.cpp  : 40x4 LCD made of two KS0066 (HD44780 compatible)

    Hardware (known):
      - CPU: Zilog Z8S180
      - ROM: M27C801 EPROM, 1 MiB (dumped, 3 identical reads)
      - LCD: 40x4 characters, same module as AlphaSmart Pro / 2000 / 3000

    TODO (pending firmware analysis, no data invented here):
      - CPU clock
      - memory map / MMU setup (CBAR, CBR, BBR) and RAM size/location
      - I/O ports for both LCD controllers
      - keyboard matrix
      - PIC microcontroller role

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
	{ }

	void writer(machine_config &config);

protected:
	virtual void machine_start() override ATTR_COLD;

private:
	required_device<z8s180_device> m_maincpu;
	required_device_array<ks0066_device, 2> m_lcdc;
	std::unique_ptr<bitmap_ind16> m_tmp_bitmap;

	void map_program(address_map &map) ATTR_COLD;
	void map_io(address_map &map) ATTR_COLD;
	void palette_init(palette_device &palette) const;
	uint32_t screen_update(screen_device &screen, bitmap_ind16 &bitmap, const rectangle &cliprect);
};


void writer_state::map_program(address_map &map)
{
	// TODO: provisional. Real ROM/RAM layout depends on the MMU setup done by the firmware.
	map(0x00000, 0xfffff).rom().region("maincpu", 0);
}

void writer_state::map_io(address_map &map)
{
	map.global_mask(0xffff);
	map(0x0000, 0x003f).noprw(); // Z180 internal registers (handled by the CPU core)
	// TODO: LCD controllers and keyboard ports
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
}


static INPUT_PORTS_START( writer )
	// TODO: keyboard matrix
INPUT_PORTS_END


void writer_state::writer(machine_config &config)
{
	Z8S180(config, m_maincpu, 20'000'000); // TODO: clock not measured
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
	ROM_REGION( 0x100000, "maincpu", 0 )
	ROM_LOAD( "akt_writer_v3.70_m27c801.bin", 0x000000, 0x100000, CRC(c454875c) SHA1(6bd0691f96c7987b2e266e220834bf4d7e3e22b4) )
ROM_END

} // anonymous namespace


//    YEAR  NAME      PARENT  COMPAT  MACHINE  INPUT   CLASS         INIT        COMPANY     FULLNAME                       FLAGS
COMP( 2005, akwriter, 0,      0,      writer,  writer, writer_state, empty_init, "AKT Inc.", "The Writer (v3.70)", MACHINE_NOT_WORKING | MACHINE_NO_SOUND )
