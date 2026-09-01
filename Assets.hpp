#pragma once

#include "PPU466.hpp"

#include <string>
#include <vector>

//Assets represents what pack-assets.cpp creates which are tiles, palettes, and levels.
//
//Stored as a chunk file. Reference read_write_chunk.hpp.
//Names and level grids vary in length, so each goes in one flat array and the
//fixed-size records hold begin/end indices into it.
//This layout is based on this course's asset pipeline lesson.

struct Assets {
	//cell codes used by level grids:
	enum Cell : uint8_t {
		CellEmpty = 0,
		CellWall = 1,
		CellDrop = 2, //a block the player has dropped which only appears at runtime
		CellExit = 3,
		CellStart = 4 //where the player's lowest block begins and is empty once loaded
	};

	struct StoredTile {
		uint32_t name_begin = 0, name_end = 0; //range in 'strings'
		uint8_t tile = 0;    //index into PPU466::tile_table
		uint8_t palette = 0; //index into PPU466::palette_table
		uint8_t padding[2] = {0, 0};
	};
	static_assert(sizeof(StoredTile) == 12, "StoredTile is packed");

	struct StoredLevel {
		uint32_t name_begin = 0, name_end = 0;   //range in 'strings'
		uint32_t cells_begin = 0, cells_end = 0; //range in 'cells', row 0 is the bottom row
		uint32_t width = 0, height = 0;
	};
	static_assert(sizeof(StoredLevel) == 24, "StoredLevel is packed");

	std::vector< PPU466::Tile > tiles;
	std::vector< PPU466::Palette > palettes;
	std::vector< char > strings;
	std::vector< StoredTile > stored_tiles;
	std::vector< StoredLevel > stored_levels;
	std::vector< uint8_t > cells;

	Assets() = default;

	//read a packed asset file, throws a runtime_error if anything is wrong with it
	Assets(std::string const &filename);

	void write(std::string const &filename) const;

	//tile lookup by the name given in tiles.txt, throws if the name isn't present
	StoredTile const &lookup(std::string const &name) const;

	std::string name_of(StoredTile const &stored) const;
	std::string name_of(StoredLevel const &stored) const;

	//copy tiles and palettes into ppu so storedTile's indices stay correct
	void upload(PPU466 *ppu_) const;
};
