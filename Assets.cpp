#include "Assets.hpp"

#include "read_write_chunk.hpp"

#include <cassert>
#include <fstream>
#include <stdexcept>

//chunk magic values where the trailing digit is a version, so stale files fail to load
static constexpr char TilesMagic[] = "til0";
static constexpr char PalettesMagic[] = "pal0";
static constexpr char StringsMagic[] = "str0";
static constexpr char TileNamesMagic[] = "tnm0";
static constexpr char LevelsMagic[] = "lvl0";
static constexpr char CellsMagic[] = "cel0";

Assets::Assets(std::string const &filename) {
	std::ifstream file(filename, std::ios::binary);
	if (!file) {
		throw std::runtime_error("Couldn't open asset file '" + filename + "'.");
	}

	read_chunk(file, TilesMagic, &tiles);
	read_chunk(file, PalettesMagic, &palettes);
	read_chunk(file, StringsMagic, &strings);
	read_chunk(file, TileNamesMagic, &stored_tiles);
	read_chunk(file, LevelsMagic, &stored_levels);
	read_chunk(file, CellsMagic, &cells);

	//since the file is data and not a programming error, check it and throw:

	if (tiles.empty() || tiles.size() > 256) {
		throw std::runtime_error("Asset file has " + std::to_string(tiles.size()) + " tiles, must be within 1 to 256.");
	}
	if (palettes.empty() || palettes.size() > 8) {
		throw std::runtime_error("Asset file has " + std::to_string(palettes.size()) + " palettes, must be within 1 to 8.");
	}
	if (stored_levels.empty()) {
		throw std::runtime_error("Asset file contains no levels.");
	}

	auto check_name = [this](uint32_t begin, uint32_t end) {
		if (begin > end || end > strings.size()) {
			throw std::runtime_error("Asset file has a name range outside the strings chunk.");
		}
	};

	for (StoredTile const &stored : stored_tiles) {
		check_name(stored.name_begin, stored.name_end);
		if (stored.tile >= tiles.size()) {
			throw std::runtime_error("Named tile '" + name_of(stored) + "' refers to a tile that isn't in the file.");
		}
		if (stored.palette >= palettes.size()) {
			throw std::runtime_error("Named tile '" + name_of(stored) + "' refers to a palette that isn't in the file.");
		}
	}

	for (StoredLevel const &stored : stored_levels) {
		check_name(stored.name_begin, stored.name_end);
		if (stored.cells_begin > stored.cells_end || stored.cells_end > cells.size()) {
			throw std::runtime_error("Level '" + name_of(stored) + "' has a cell range outside the cells chunk.");
		}
		if (stored.width == 0 || stored.height == 0
		 || stored.width > PPU466::ScreenWidth / 8 || stored.height > PPU466::ScreenHeight / 8) {
			throw std::runtime_error("Level '" + name_of(stored) + "' doesn't fit on screen.");
		}
		if (stored.width * stored.height != stored.cells_end - stored.cells_begin) {
			throw std::runtime_error("Level '" + name_of(stored) + "' has a size that disagrees with its cell count.");
		}
	}

	for (uint8_t cell : cells) {
		if (cell > CellStart) {
			throw std::runtime_error("Asset file contains an unknown cell code.");
		}
	}
}

void Assets::write(std::string const &filename) const {
	std::ofstream file(filename, std::ios::binary);
	if (!file) {
		throw std::runtime_error("Couldn't open '" + filename + "' for writing.");
	}

	write_chunk(TilesMagic, tiles, &file);
	write_chunk(PalettesMagic, palettes, &file);
	write_chunk(StringsMagic, strings, &file);
	write_chunk(TileNamesMagic, stored_tiles, &file);
	write_chunk(LevelsMagic, stored_levels, &file);
	write_chunk(CellsMagic, cells, &file);

	if (!file) {
		throw std::runtime_error("Something went wrong writing '" + filename + "'.");
	}
}

Assets::StoredTile const &Assets::lookup(std::string const &name) const {
	for (StoredTile const &stored : stored_tiles) {
		if (name_of(stored) == name) return stored;
	}
	throw std::runtime_error("No tile named '" + name + "' in the asset file.");
}

std::string Assets::name_of(StoredTile const &stored) const {
	assert(stored.name_end <= strings.size()); //ranges are checked when the file is read
	return std::string(strings.begin() + stored.name_begin, strings.begin() + stored.name_end);
}

std::string Assets::name_of(StoredLevel const &stored) const {
	assert(stored.name_end <= strings.size()); //ranges are checked when the file is read
	return std::string(strings.begin() + stored.name_begin, strings.begin() + stored.name_end);
}

void Assets::upload(PPU466 *ppu_) const {
	assert(ppu_ && "a ppu to upload into is required.");
	auto &ppu = *ppu_;

	//both of these are checked when the file is read:
	assert(tiles.size() <= ppu.tile_table.size());
	assert(palettes.size() <= ppu.palette_table.size());

	for (uint32_t i = 0; i < tiles.size(); ++i) {
		ppu.tile_table[i] = tiles[i];
	}
	for (uint32_t i = 0; i < palettes.size(); ++i) {
		ppu.palette_table[i] = palettes[i];
	}
}
