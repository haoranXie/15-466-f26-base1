#include "PPU466.hpp"
#include "Mode.hpp"
#include "Assets.hpp"

#include <glm/glm.hpp>

#include <vector>
#include <deque>

struct PlayMode : Mode {
	PlayMode();
	virtual ~PlayMode();

	//functions called by main loop:
	virtual bool handle_event(SDL_Event const &, glm::uvec2 const &window_size) override;
	virtual void update(float elapsed) override;
	virtual void draw(glm::uvec2 const &drawable_size) override;

	//----- game state -----

	//the player starts every room as this many stacked blocks:
	enum : int32_t { StartHeight = 4 };

	//input tracking:
	struct Button {
		uint8_t downs = 0;
		uint8_t pressed = 0;
	} left, right, place, restart;

	uint32_t level_index = 0;
	glm::ivec2 level_size = glm::ivec2(0, 0);
	glm::ivec2 level_origin = glm::ivec2(0, 0); 
	std::vector< uint8_t > cells; //row 0 is the bottom row

	//the player is aa stack of blocks with feet as is the lowest one:
	glm::ivec2 feet = glm::ivec2(0, 0);
	int32_t height = StartHeight;
	int32_t facing = 1; //-1 or 1 defines whether you place a block left or right

	bool finished = false;

	void load_level(uint32_t index);

	bool solid(int32_t x, int32_t y) const;
	bool fits(int32_t x, int32_t y, int32_t h) const;

	void settle();
	void step(int32_t dx); 
	void place_block(); 
	bool on_exit() const;

	//----- drawing handled by PPU466 -----

	PPU466 ppu;

	Assets::StoredTile empty_tile, wall_tile, wall_top_tile, drop_tile, exit_tile;
	Assets::StoredTile head_tile, body_tile, foot_tile;
};
