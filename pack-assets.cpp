//pack-assets reads the art and level text in assets/ and writes dist/assets.chunk.
//Run it from the game directory after changing:
//assets/tiles.png which is a grid of 8x8 cells representing tiles
//assets/tiles.txt which is a list of names for each from left to right
//assets/levels.txt which is a each level

#include "Assets.hpp"
#include "load_save_png.hpp"

#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
#include <algorithm>
#include <cassert>
#include <fstream>

namespace {

//to ensure packing is deterministic a color is compared and sorted as a single number:
uint32_t pack_color(glm::u8vec4 const &color) {
	return (uint32_t(color.r) << 24) | (uint32_t(color.g) << 16) | (uint32_t(color.b) << 8) | uint32_t(color.a);
}

//everything fully transparent is the same color:
glm::u8vec4 canonical(glm::u8vec4 const &color) {
	if (color.a == 0) return glm::u8vec4(0x00, 0x00, 0x00, 0x00);
	return color;
}

//is this color already in the palette? only its first 'count' entries are filled in:
bool palette_holds(PPU466::Palette const &palette, uint32_t count, glm::u8vec4 const &color) {
	for (uint32_t i = 0; i < count; ++i) {
		if (pack_color(palette[i]) == pack_color(color)) return true;
	}
	return false;
}

//getline loop follows the credits-file example in the asset pipeline lesson:
std::vector< std::string > read_lines(std::string const &filename) {
	std::ifstream file(filename, std::ios::binary);
	if (!file) {
		throw std::runtime_error("Couldn't open '" + filename + "'.");
	}
	std::vector< std::string > lines;
	for (std::string line; std::getline(file, line); ) {
		if (!line.empty() && line.back() == '\r') line.pop_back(); //tolerate crlf
		lines.emplace_back(line);
	}
	return lines;
}

//append a name to the strings chunk and hand back the range it occupies:
void append_name(std::string const &name, std::vector< char > *strings_, uint32_t *begin_, uint32_t *end_) {
	auto &strings = *strings_;
	*begin_ = uint32_t(strings.size());
	strings.insert(strings.end(), name.begin(), name.end());
	*end_ = uint32_t(strings.size());
}

void pack_tiles(Assets *assets_) {
	auto &assets = *assets_;

	glm::uvec2 size;
	std::vector< glm::u8vec4 > png;
	load_png("assets/tiles.png", &size, &png, LowerLeftOrigin);

	if (size.x == 0 || size.y == 0 || size.x % 8 != 0 || size.y % 8 != 0) {
		throw std::runtime_error("assets/tiles.png is " + std::to_string(size.x) + "x" + std::to_string(size.y)
			+ "; both dimensions must be nonzero multiples of 8.");
	}
	uint32_t const columns = size.x / 8;
	uint32_t const rows = size.y / 8;

	//how many entries of each palette are filled in; the rest can still be given out:
	std::vector< uint32_t > palette_sizes;

	//names are listed in reading order, so the first name is the top-left cell:
	std::vector< std::string > names;
	for (std::string const &line : read_lines("assets/tiles.txt")) {
		if (line.empty() || line[0] == '#') continue;
		names.emplace_back(line);
	}
	if (names.size() != columns * rows) {
		throw std::runtime_error("assets/tiles.png holds " + std::to_string(columns * rows) + " cells but assets/tiles.txt names " + std::to_string(names.size()) + ".");
	}

	for (uint32_t index = 0; index < names.size(); ++index) {
		uint32_t const column = index % columns;
		uint32_t const row_from_bottom = rows - 1 - index / columns;

		//copy the cell out of the sheet and treat every transparent pixel the same
		std::vector< glm::u8vec4 > cell(8 * 8);
		for (uint32_t y = 0; y < 8; ++y) {
			for (uint32_t x = 0; x < 8; ++x) {
				cell[x + 8 * y] = canonical(png[(column * 8 + x) + size.x * (row_from_bottom * 8 + y)]);
			}
		}

		//list the distinct colors this cell uses
		std::vector< glm::u8vec4 > used;
		for (glm::u8vec4 const &color : cell) {
			bool seen = false;
			for (glm::u8vec4 const &other : used) {
				if (pack_color(other) == pack_color(color)) seen = true;
			}
			if (!seen) used.emplace_back(color);
		}
		if (used.size() > 4) {
			throw std::runtime_error("Tile '" + names[index] + "' (column " + std::to_string(column)
				+ ", row " + std::to_string(index / columns) + " of assets/tiles.png) uses "
				+ std::to_string(used.size()) + " colors; the ppu allows 4 per tile, counting transparent.");
		}

		//only eight palettes exist, so prefer one that already covers these colors,
		//then one with room to grow, then a new one. entries are never
		//re-ordered so tiles packed earlier already refer to them by index.
		uint32_t palette_index = uint32_t(assets.palettes.size());
		for (uint32_t p = 0; p < assets.palettes.size() && palette_index == assets.palettes.size(); ++p) {
			bool covered = true;
			for (glm::u8vec4 const &color : used) {
				if (!palette_holds(assets.palettes[p], palette_sizes[p], color)) covered = false;
			}
			if (covered) palette_index = p;
		}
		for (uint32_t p = 0; p < assets.palettes.size() && palette_index == assets.palettes.size(); ++p) {
			std::vector< glm::u8vec4 > missing;
			for (glm::u8vec4 const &color : used) {
				if (!palette_holds(assets.palettes[p], palette_sizes[p], color)) missing.emplace_back(color);
			}
			if (palette_sizes[p] + missing.size() > 4) continue;
			for (glm::u8vec4 const &color : missing) {
				assets.palettes[p][palette_sizes[p]] = color;
				palette_sizes[p] += 1;
			}
			palette_index = p;
		}
		if (palette_index == assets.palettes.size()) {
			if (assets.palettes.size() == 8) {
				throw std::runtime_error("Ran out of palettes at tile '" + names[index]
					+ "'; the ppu has 8 and the sheet needs more. Share colors between tiles.");
			}
			//transparent packs to zero, so sorting puts it at index 0 when present:
			std::sort(used.begin(), used.end(), [](glm::u8vec4 const &a, glm::u8vec4 const &b) {
				return pack_color(a) < pack_color(b);
			});
			PPU466::Palette palette;
			palette.fill(glm::u8vec4(0x00, 0x00, 0x00, 0x00));
			for (uint32_t i = 0; i < used.size(); ++i) {
				palette[i] = used[i];
			}
			assets.palettes.emplace_back(palette);
			palette_sizes.emplace_back(uint32_t(used.size()));
		}

		//write the cell as the two bit planes the ppu wants; this inverts the
		//pixel-readback example in PPU466.hpp's Tile comment:
		PPU466::Tile tile;
		tile.bit0.fill(0);
		tile.bit1.fill(0);
		for (uint32_t y = 0; y < 8; ++y) {
			for (uint32_t x = 0; x < 8; ++x) {
				uint8_t color_index = 4;
				for (uint8_t i = 0; i < 4; ++i) {
					if (pack_color(assets.palettes[palette_index][i]) == pack_color(cell[x + 8 * y])) {
						color_index = i;
						break;
					}
				}
				//the palette was chosen to cover this cell, so a miss is a bug, not bad input:
				assert(color_index < 4 && "every pixel of a tile is in the palette picked for it");
				if (color_index & 1) tile.bit0[y] |= uint8_t(1 << x);
				if (color_index & 2) tile.bit1[y] |= uint8_t(1 << x);
			}
		}

		Assets::StoredTile stored;
		append_name(names[index], &assets.strings, &stored.name_begin, &stored.name_end);
		stored.tile = uint8_t(assets.tiles.size());
		stored.palette = uint8_t(palette_index);
		assets.tiles.emplace_back(tile);
		assets.stored_tiles.emplace_back(stored);
	}
}

void pack_levels(Assets *assets_) {
	auto &assets = *assets_;

	std::string name;
	std::vector< std::string > rows;

	//turn the rows into a level where the ppu counts rows from the bottom,
	//so they get stored and flipped
	auto flush = [&assets, &name, &rows]() {
		if (name.empty() && rows.empty()) return;
		if (rows.empty()) {
			throw std::runtime_error("Level '" + name + "' has no rows.");
		}

		Assets::StoredLevel stored;
		append_name(name, &assets.strings, &stored.name_begin, &stored.name_end);
		stored.width = uint32_t(rows[0].size());
		stored.height = uint32_t(rows.size());
		stored.cells_begin = uint32_t(assets.cells.size());

		uint32_t starts = 0;
		uint32_t exits = 0;
		for (uint32_t y = 0; y < rows.size(); ++y) {
			std::string const &row = rows[rows.size() - 1 - y];
			if (row.size() != stored.width) {
				throw std::runtime_error("Level '" + name + "' has rows of differing width.");
			}
			for (char c : row) {
				if (c == '.' || c == ' ') {
					assets.cells.emplace_back(Assets::CellEmpty);
				} else if (c == '#') {
					assets.cells.emplace_back(Assets::CellWall);
				} else if (c == 'P') {
					assets.cells.emplace_back(Assets::CellStart);
					++starts;
				} else if (c == 'E') {
					assets.cells.emplace_back(Assets::CellExit);
					++exits;
				} else {
					throw std::runtime_error("Level '" + name + "' contains the character '" + std::string(1, c)
						+ "', which isn't one of . # P E.");
				}
			}
		}
		stored.cells_end = uint32_t(assets.cells.size());

		if (starts != 1) {
			throw std::runtime_error("Level '" + name + "' has " + std::to_string(starts)
				+ " start positions; it needs exactly one 'P'.");
		}
		if (exits == 0) {
			throw std::runtime_error("Level '" + name + "' has no exit; it needs at least one 'E'.");
		}

		assets.stored_levels.emplace_back(stored);
		name.clear();
		rows.clear();
	};

	//';' starts a comment here rather than '#', because '#' is a wall:
	for (std::string const &line : read_lines("assets/levels.txt")) {
		if (line.empty() || line[0] == ';') continue;
		if (line.rfind("level ", 0) == 0) {
			flush();
			name = line.substr(6);
		} else {
			if (name.empty()) {
				throw std::runtime_error("assets/levels.txt has rows before its first 'level' line.");
			}
			rows.emplace_back(line);
		}
	}
	flush();
}

}

int main(int argc, char **argv) {
	(void)argc;
	(void)argv;

	try {
		Assets assets;
		pack_tiles(&assets);
		pack_levels(&assets);
		assets.write("dist/assets.chunk");

		std::cout << "Packed " << assets.tiles.size() << " tiles into " << assets.palettes.size()
			<< " palettes, and " << assets.stored_levels.size() << " levels, to dist/assets.chunk." << std::endl;
	} catch (std::exception const &e) {
		std::cerr << "Failed to pack assets:\n  " << e.what() << std::endl;
		return 1;
	}

	return 0;
}
