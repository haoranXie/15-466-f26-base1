#include "PlayMode.hpp"

#include "Load.hpp"
#include "data_path.hpp"

#include <cassert>

//load assets before any game code runs so a broken asset file fails at startup:
static Load<Assets> assets(LoadTagDefault, []() -> Assets const * {
	return new Assets(data_path("assets.chunk"));
});

PlayMode::PlayMode() {
	assets->upload(&ppu);

	empty_tile = assets->lookup("empty");
	wall_tile = assets->lookup("wall");
	wall_top_tile = assets->lookup("wall_top");
	drop_tile = assets->lookup("dropped");
	exit_tile = assets->lookup("exit");
	head_tile = assets->lookup("player_head");
	body_tile = assets->lookup("player_body");
	foot_tile = assets->lookup("player_foot");

	load_level(0);
}

PlayMode::~PlayMode() {
}

void PlayMode::load_level(uint32_t index) {
	assert(index<assets->stored_levels.size() && "all levels exist");
	Assets::StoredLevel const &level = assets->stored_levels[index];

	level_index = index;
	level_size = glm::ivec2(int32_t(level.width), int32_t(level.height));
	cells.assign(assets->cells.begin() + level.cells_begin, assets->cells.begin() + level.cells_end);

	//center the room on screen
	level_origin = glm::ivec2(
		(int32_t(PPU466::ScreenWidth / 8) - level_size.x) / 2,
		(int32_t(PPU466::ScreenHeight / 8) - level_size.y) / 2
	);

	//the start marker is just a place to stand once the room is loaded:
	bool found = false;
	for (int32_t y = 0; y < level_size.y; ++y) {
		for (int32_t x = 0; x < level_size.x; ++x) {
			if (cells[x + level_size.x * y] == Assets::CellStart) {
				cells[x + level_size.x * y] = Assets::CellEmpty;
				feet = glm::ivec2(x, y);
				found = true;
			}
		}
	}
	assert(found && "rejecs every level without a start mark");
	(void)found;

	height = StartHeight;
	facing = 1;
	settle();
}

bool PlayMode::solid(int32_t x, int32_t y) const {
	if (x < 0 || y < 0 || x >= level_size.x || y >= level_size.y) return true;
	uint8_t cell = cells[x + level_size.x * y];
	return cell == Assets::CellWall || cell == Assets::CellDrop;
}

bool PlayMode::fits(int32_t x, int32_t y, int32_t h) const {
	for (int32_t i = 0; i < h; ++i) {
		if (solid(x, y + i)) return false;
	}
	return true;
}

void PlayMode::settle() {
	//solid() is true below the floor, so this always stops:
	while (!solid(feet.x, feet.y - 1)) {
		feet.y -= 1;
	}
}

void PlayMode::step(int32_t dx) {
	int32_t const to = feet.x + dx;

	if (fits(to, feet.y, height)) {
		feet.x = to;
		settle();
		return;
	}

	//only step up tiles that are 1 tile height
	if (solid(to, feet.y) && fits(to, feet.y + 1, height)) {
		feet.x = to;
		feet.y += 1;
		return;
	}
}

void PlayMode::place_block() {
	if (height <= 1) return; //only the player block is left

	int32_t const x = feet.x + facing;
	int32_t const y = feet.y;
	if (x < 0 || y < 0 || x >= level_size.x || y >= level_size.y) return;
	if (cells[x + level_size.x * y] != Assets::CellEmpty) return;

	cells[x + level_size.x * y] = Assets::CellDrop;

	height -= 1;
	settle();
}

bool PlayMode::on_exit() const {
	for (int32_t i = 0; i < height; ++i) {
		int32_t const y = feet.y + i;
		if (y < 0 || y >= level_size.y) continue;
		if (cells[feet.x + level_size.x * y] == Assets::CellExit) return true;
	}
	return false;
}

bool PlayMode::handle_event(SDL_Event const &evt, glm::uvec2 const &window_size) {
	(void)window_size;

	//repeats are ignored or else holding the down key could remove player.
	//based on https://wiki.libsdl.org/SDL3/SDL_KeyboardEvent for repeat:
	if (evt.type == SDL_EVENT_KEY_DOWN && !evt.key.repeat) {
		if (evt.key.key == SDLK_LEFT) {
			left.downs += 1;
			left.pressed = true;
			return true;
		} else if (evt.key.key == SDLK_RIGHT) {
			right.downs += 1;
			right.pressed = true;
			return true;
		} else if (evt.key.key == SDLK_DOWN) {
			place.downs += 1;
			place.pressed = true;
			return true;
		} else if (evt.key.key == SDLK_R) {
			restart.downs += 1;
			restart.pressed = true;
			return true;
		}
	} else if (evt.type == SDL_EVENT_KEY_UP) {
		if (evt.key.key == SDLK_LEFT) {
			left.pressed = false;
			return true;
		} else if (evt.key.key == SDLK_RIGHT) {
			right.pressed = false;
			return true;
		} else if (evt.key.key == SDLK_DOWN) {
			place.pressed = false;
			return true;
		} else if (evt.key.key == SDLK_R) {
			restart.pressed = false;
			return true;
		}
	}

	return false;
}

void PlayMode::update(float elapsed) {
	//changed the movement to be discrete meaning I don't need elapsed
	(void)elapsed;

	//consume presses one at a time so a slow frame plays them all back
	for (; restart.downs > 0; --restart.downs) {
		if (finished) {
			finished = false;
			load_level(0);
		} else {
			load_level(level_index);
		}
	}

	if (finished) {
		//there is no room to move around in; drop anything else that arrived:
		left.downs = 0;
		right.downs = 0;
		place.downs = 0;
		return;
	}

	for (; left.downs > 0; --left.downs) {
		facing = -1;
		step(-1);
	}
	for (; right.downs > 0; --right.downs) {
		facing = 1;
		step(1);
	}
	for (; place.downs > 0; --place.downs) {
		place_block();
	}

	if (on_exit()) {
		if (level_index + 1 < uint32_t(assets->stored_levels.size())) {
			load_level(level_index + 1);
		} else {
			finished = true;
		}
	}
}

void PlayMode::draw(glm::uvec2 const &drawable_size) {
	ppu.background_color = glm::u8vec3(0x00, 0x00, 0x00);
	ppu.background_position = glm::ivec2(0, 0);

	//tile index in bits 0-7, palette in bits 8-10.
	//layout from the background layer comment of PPU466.hpp:
	auto put = [this](int32_t x, int32_t y, Assets::StoredTile const &tile) {
		assert(x >= 0 && y >= 0);
		assert(x < int32_t(PPU466::BackgroundWidth) && y < int32_t(PPU466::BackgroundHeight));
		ppu.background[x + PPU466::BackgroundWidth * y] = uint16_t(tile.tile) | uint16_t(uint16_t(tile.palette) << 8);
	};

	for (uint32_t i = 0; i < ppu.background.size(); ++i) {
		ppu.background[i] = uint16_t(empty_tile.tile) | uint16_t(uint16_t(empty_tile.palette) << 8);
	}

	if (finished) {
		//every room cleared creates a floor of exits with the player on it.
		int32_t const floor_y = 12;
		for (int32_t x = 0; x < int32_t(PPU466::ScreenWidth / 8); ++x) {
			put(x, floor_y, exit_tile);
		}

		uint32_t won_sprite = 0;
		for (int32_t i = 0; i < StartHeight; ++i, ++won_sprite) {
			Assets::StoredTile const *tile = &body_tile;
			if (i == 0) {
				tile = &foot_tile;
			} else if (i == StartHeight - 1) {
				tile = &head_tile;
			}

			ppu.sprites[won_sprite].x = uint8_t(16 * 8);
			ppu.sprites[won_sprite].y = uint8_t((floor_y + 1 + i) * 8);
			ppu.sprites[won_sprite].index = tile->tile;
			ppu.sprites[won_sprite].attributes = tile->palette;
		}
		for (; won_sprite < ppu.sprites.size(); ++won_sprite) {
			ppu.sprites[won_sprite].y = 240;
		}

		ppu.draw(drawable_size);
		return;
	}

	for (int32_t y = 0; y < level_size.y; ++y) {
		for (int32_t x = 0; x < level_size.x; ++x) {
			uint8_t const cell = cells[x + level_size.x * y];
			if (cell == Assets::CellWall) {
				//walls get a different top for making it clear what you can walk on
				put(level_origin.x + x, level_origin.y + y, solid(x, y + 1) ? wall_tile : wall_top_tile);
			} else if (cell == Assets::CellDrop) {
				put(level_origin.x + x, level_origin.y + y, drop_tile);
			} else if (cell == Assets::CellExit) {
				put(level_origin.x + x, level_origin.y + y, exit_tile);
			}
		}
	}

	//the player is the only thing drawn with sprites with one per block
	uint32_t sprite = 0;
	for (int32_t i = 0; i < height && sprite < ppu.sprites.size(); ++i, ++sprite) {
		Assets::StoredTile const *tile = &body_tile;
		if (height == 1 || i == height - 1) {
			tile = &head_tile;
		} else if (i == 0) {
			tile = &foot_tile;
		}

		ppu.sprites[sprite].x = uint8_t((level_origin.x + feet.x) * 8);
		ppu.sprites[sprite].y = uint8_t((level_origin.y + feet.y + i) * 8);
		ppu.sprites[sprite].index = tile->tile;
		ppu.sprites[sprite].attributes = tile->palette; //priority 0: in front of the background
	}
	for (; sprite < ppu.sprites.size(); ++sprite) {
		ppu.sprites[sprite].y = 240;
	}

	ppu.draw(drawable_size);
}
