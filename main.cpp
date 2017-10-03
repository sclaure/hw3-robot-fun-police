#include "load_save_png.hpp"
#include "GL.hpp"
#include "Meshes.hpp"
#include "Scene.hpp"
#include "read_chunk.hpp"
#include <math.h>

#include <SDL.h>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <chrono>
#include <iostream>
#include <stdexcept>
#include <fstream>

static GLuint compile_shader(GLenum type, std::string const &source);
static GLuint link_program(GLuint vertex_shader, GLuint fragment_shader);

int main(int argc, char **argv) {
	//Configuration:
	struct {
		std::string title = "Game2: Scene";
		glm::uvec2 size = glm::uvec2(640, 480);
	} config;

	//------------  initialization ------------

	//Initialize SDL library:
	SDL_Init(SDL_INIT_VIDEO);

	//Ask for an OpenGL context version 3.3, core profile, enable debug:
	SDL_GL_ResetAttributes();
	SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
	SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
	SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
	SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 8);
	SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
	SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
	SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_DEBUG_FLAG);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);

	//create window:
	SDL_Window *window = SDL_CreateWindow(
		config.title.c_str(),
		SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
		config.size.x, config.size.y,
		SDL_WINDOW_OPENGL /*| SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI*/
	);

	if (!window) {
		std::cerr << "Error creating SDL window: " << SDL_GetError() << std::endl;
		return 1;
	}

	//Create OpenGL context:
	SDL_GLContext context = SDL_GL_CreateContext(window);

	if (!context) {
		SDL_DestroyWindow(window);
		std::cerr << "Error creating OpenGL context: " << SDL_GetError() << std::endl;
		return 1;
	}

	#ifdef _WIN32
	//On windows, load OpenGL extensions:
	if (!init_gl_shims()) {
		std::cerr << "ERROR: failed to initialize shims." << std::endl;
		return 1;
	}
	#endif

	//Set VSYNC + Late Swap (prevents crazy FPS):
	if (SDL_GL_SetSwapInterval(-1) != 0) {
		std::cerr << "NOTE: couldn't set vsync + late swap tearing (" << SDL_GetError() << ")." << std::endl;
		if (SDL_GL_SetSwapInterval(1) != 0) {
			std::cerr << "NOTE: couldn't set vsync (" << SDL_GetError() << ")." << std::endl;
		}
	}

	//Hide mouse cursor (note: showing can be useful for debugging):
	//SDL_ShowCursor(SDL_DISABLE);

	//------------ opengl objects / game assets ------------

	//shader program:
	GLuint program = 0;
	GLuint program_Position = 0;
	GLuint program_Normal = 0;
	GLuint program_mvp = 0;
	GLuint program_itmv = 0;
	GLuint program_to_light = 0;
	{ //compile shader program:
		GLuint vertex_shader = compile_shader(GL_VERTEX_SHADER,
			"#version 330\n"
			"uniform mat4 mvp;\n"
			"uniform mat3 itmv;\n"
			"in vec4 Position;\n"
			"in vec3 Normal;\n"
			"out vec3 normal;\n"
			"void main() {\n"
			"	gl_Position = mvp * Position;\n"
			"	normal = itmv * Normal;\n"
			"}\n"
		);

		GLuint fragment_shader = compile_shader(GL_FRAGMENT_SHADER,
			"#version 330\n"
			"uniform vec3 to_light;\n"
			"in vec3 normal;\n"
			"out vec4 fragColor;\n"
			"void main() {\n"
			"	float light = max(0.0, dot(normalize(normal), to_light));\n"
			"	fragColor = vec4(light * vec3(1.0, 1.0, 1.0), 1.0);\n"
			"}\n"
		);

		program = link_program(fragment_shader, vertex_shader);

		//look up attribute locations:
		program_Position = glGetAttribLocation(program, "Position");
		if (program_Position == -1U) throw std::runtime_error("no attribute named Position");
		program_Normal = glGetAttribLocation(program, "Normal");
		if (program_Normal == -1U) throw std::runtime_error("no attribute named Normal");

		//look up uniform locations:
		program_mvp = glGetUniformLocation(program, "mvp");
		if (program_mvp == -1U) throw std::runtime_error("no uniform named mvp");
		program_itmv = glGetUniformLocation(program, "itmv");
		if (program_itmv == -1U) throw std::runtime_error("no uniform named itmv");

		program_to_light = glGetUniformLocation(program, "to_light");
		if (program_to_light == -1U) throw std::runtime_error("no uniform named to_light");
	}

	//------------ meshes ------------

	Meshes meshes;

	{ //add meshes to database:
		Meshes::Attributes attributes;
		attributes.Position = program_Position;
		attributes.Normal = program_Normal;

		meshes.load("meshes.blob", attributes);
	}
	
	//------------ scene ------------

	Scene scene;
	//set up camera parameters based on window:
	scene.camera.fovy = glm::radians(60.0f);
	scene.camera.aspect = float(config.size.x) / float(config.size.y);
	scene.camera.near = 0.01f;
	//(transform will be handled in the update function below)

	//add some objects from the mesh library:
	auto add_object = [&](std::string const &name, glm::vec3 const &position, glm::quat const &rotation, glm::vec3 const &scale) -> Scene::Object & {
		Mesh const &mesh = meshes.get(name);
		scene.objects.emplace_back();
		Scene::Object &object = scene.objects.back();
		object.transform.position = position;
		object.transform.rotation = rotation;
		object.transform.scale = scale;
		object.vao = mesh.vao;
		object.start = mesh.start;
		object.count = mesh.count;
		object.program = program;
		object.program_mvp = program_mvp;
		object.program_itmv = program_itmv;
		return object;
	};


	// our tree stack here is our robot arm
	std::vector< Scene::Object * > players;
	Scene::Object *net;
	Scene::Object *floor;
	Scene::Object *ball;
	std::vector< Scene::Object * > walls;


	{ //read objects to add from "scene.blob":
		std::ifstream file("scene.blob", std::ios::binary);

		std::vector< char > strings;
		//read strings chunk:
		read_chunk(file, "str0", &strings);

		{ //read scene chunk, add meshes to scene:
			struct SceneEntry {
				uint32_t name_begin, name_end;
				glm::vec3 position;
				glm::quat rotation;
				glm::vec3 scale;
			};
			static_assert(sizeof(SceneEntry) == 48, "Scene entry should be packed");

			std::vector< SceneEntry > data;
			read_chunk(file, "scn0", &data);

			for (auto const &entry : data) {
				if (!(entry.name_begin <= entry.name_end && entry.name_end <= strings.size())) {
					throw std::runtime_error("index entry has out-of-range name begin/end");
				}
				std::string name(&strings[0] + entry.name_begin, &strings[0] + entry.name_end);

				if (name == "Cube" ||
					name == "Cube.001"){
					players.emplace_back( &add_object(name, entry.position, entry.rotation, entry.scale) );
				}
				else if (name == "Cube.002" ){
					net = &add_object(name, entry.position, entry.rotation, entry.scale);
				}
				else if (name == "Plane"){
					floor = &add_object(name, entry.position, entry.rotation, entry.scale);
				}
				else if (name == "Sphere"){
					ball = &add_object(name, entry.position, entry.rotation, entry.scale);
				}
				else{
					walls.emplace_back( &add_object(name, entry.position, entry.rotation, entry.scale) );
				}
			}
		}
	}

	glm::vec2 mouse = glm::vec2(0.0f, 0.0f); //mouse position in [-1,1]x[-1,1] coordinates

	struct {
		float radius = 15.0f;
		float elevation = 0.3f;
		float azimuth = 0.0f;
		glm::vec3 target = glm::vec3(0.0f, 0.0f, 0.0f);
	} camera;

	bool p1_right = false;
	bool p1_left = false;
	bool p2_right = false;
	bool p2_left = false;

	bool p1_can_jump = true;
	bool p2_can_jump = true;
	bool p1_jumped = false;
	bool p2_jumped = false;

	float p1_c1_x, p1_c1_y;
	float p1_c2_x, p1_c2_y;
	float p2_c1_x, p2_c1_y;
	float p2_c2_x, p2_c2_y;
	float net_c_x, net_c_y;

	float ball_pos_x, ball_pos_y;
	float distance;

	//booleans to reduce unnecessary calculations
	bool hit_corner = false;
	bool hit_top = false;
	bool hit_side = false;

	//whole game may be 3d, but is bound by 2d controls
	//x coordinates correspond to an objects y position
	//y coordinates correspond to an objects z position 

	//player 1 and 2 can only exert vertical velocity (horizontal velocity is fixed by the users)
	float p1_vel_y = 0.0f;
	float p2_vel_y = 0.0f;

	float ball_vel_x = 0.0f;
	float ball_vel_y = 0.0f;

	float gravity = -10.0f;

	int p1_score = 0;
	int p2_score = 0;

	bool game_over = false;

	bool p1_touch_last = false;

	//------------ game loop ------------

	bool should_quit = false;
	while (true) {
		static SDL_Event evt;
		while (SDL_PollEvent(&evt) == 1) {
			//handle input:
			if (evt.type == SDL_MOUSEMOTION) {
				/*glm::vec2 old_mouse = mouse;
				mouse.x = (evt.motion.x + 0.5f) / float(config.size.x) * 2.0f - 1.0f;
				mouse.y = (evt.motion.y + 0.5f) / float(config.size.y) *-2.0f + 1.0f;
				if (evt.motion.state & SDL_BUTTON(SDL_BUTTON_LEFT)) {
					camera.elevation += -2.0f * (mouse.y - old_mouse.y);
					camera.azimuth += -2.0f * (mouse.x - old_mouse.x);
				}*/
			} else if (evt.type == SDL_MOUSEBUTTONDOWN) {
			} else if (evt.type == SDL_KEYDOWN && evt.key.keysym.sym == SDLK_ESCAPE) {
				should_quit = true;
			} else if (evt.type == SDL_QUIT) {
				should_quit = true;
				break;
			}
		}
		if (should_quit) break;

		// record a snapshot of the keyboard state
		const Uint8 *state = SDL_GetKeyboardState(NULL);
		if (state[SDL_SCANCODE_A]) {
			if (players[0]->transform.position[1] > -9.5f){
		    	players[0]->transform.position[1] -= 0.1f;
		    	p1_left = true;
		    }
		    p1_left = false;
		} else {
			p1_left = false;
		}
		if (state[SDL_SCANCODE_D]) {
			if (players[0]->transform.position[1] < -0.55f){
		    	players[0]->transform.position[1] += 0.1f; 
		    	p1_right = true;
		    }
		    p1_right = false;
		} else {
			p1_right = false;
		}
		if (state[SDL_SCANCODE_W]) {
			if (p1_can_jump){
		    	p1_vel_y = 6.0f;
		    	p1_can_jump = false;
		    	p1_jumped = true;
		    }
		}
		if (state[SDL_SCANCODE_LEFT]) {
			if (players[1]->transform.position[1] > 0.55f){
		    	players[1]->transform.position[1] -= 0.1f; 
		    	p2_left = true;
		    }
		    p2_left = false;
		} else {
			p2_left = false;
		}
		if (state[SDL_SCANCODE_RIGHT]) {
			if (players[1]->transform.position[1] < 9.5f){
		    	players[1]->transform.position[1] += 0.1f; 
		    	p2_right = true;
			}
			p2_right = false;
		} else {
			p2_right = false;
		}
		if (state[SDL_SCANCODE_UP]) {
			if (p2_can_jump){
		    	p2_vel_y = 6.0f;
		    	p2_can_jump = false;
		    	p2_jumped = true;
		    }
		}

		//collision detection calculations

		{ //update game state:
			//update player 1 (divide calculations by framerate, i.e. 60fps)
			//don't let the player fall through the floor
			if ((players[0]->transform.position[2] != 0.5f) || p1_jumped){
				players[0]->transform.position[2] += (p1_vel_y / 60.0f);

				//if the player reached the floor, reset velocity and z position
				if (players[0]->transform.position[2] <= 0.5f){
					players[0]->transform.position[2] = 0.5f;
					p1_vel_y = 0.0f;
					p1_can_jump = true;
				}

				p1_jumped = false;
			}

			//update player 2 (divide calculations by framerate, i.e. 60fps)
			//don't let the player fall through the floor
			if ((players[1]->transform.position[2] != 0.5f) || p2_jumped){
				players[1]->transform.position[2] += (p2_vel_y / 60.0f);

				//if the player reached the floor, reset velocity and z position
				if (players[1]->transform.position[2] <= 0.5f){
					players[1]->transform.position[2] = 0.5f;
					p2_vel_y = 0.0f;
					p2_can_jump = true;
				}

				p2_jumped = false;
			}

			//update ball's velocity
			ball->transform.position[2] += (ball_vel_y / 60.0f);
			ball->transform.position[1] += (ball_vel_x / 60.0f);

			//if the ball reached the left wall, reverse the x direction
			if (ball->transform.position[1] <= -9.15f){
				ball->transform.position[1] = -9.15f;
				ball_vel_x *= -1.0f;
			}

			//if the ball reached the right wall, reverse the x direction
			if (ball->transform.position[1] >= 9.15f){
				ball->transform.position[1] = 9.15f;
				ball_vel_x *= -1.0f;
			}


			//grab ball position
			ball_pos_x = ball->transform.position[1];
			ball_pos_y = ball->transform.position[2];

			hit_corner = false;

			/*check if the ball hits one of the corners of a player first*/
			//player 1 corners
			p1_c1_x = players[0]->transform.position[1] - 0.5f;
			p1_c1_y = players[0]->transform.position[2] + 0.5f;

			distance = sqrt(pow((ball_pos_x - p1_c1_x),2) + pow((ball_pos_y - p1_c1_y),2));

			if ((distance <= 0.35f) && !hit_corner){
				hit_corner = true;
				ball_vel_y = 8.0f;
				ball_vel_x -= 1.5f;
				ball_vel_x += -1.0f * p1_left;
				ball_vel_x += 1.0f * p1_right;

				p1_touch_last = true;
			}

			p1_c2_x = players[0]->transform.position[1] + 0.5f;
			p1_c2_y = players[0]->transform.position[2] + 0.5f;

			distance = sqrt(pow((ball_pos_x - p1_c2_x),2) + pow((ball_pos_y - p1_c2_y),2));

			if ((distance <= 0.35f) && !hit_corner){
				hit_corner = true;
				ball_vel_y = 8.0f;
				ball_vel_x += 1.5f;
				ball_vel_x += -1.0f * p1_left;
				ball_vel_x += 1.0f * p1_right;

				p1_touch_last = true;
			}

			//player 2 corners
			p2_c1_x = players[1]->transform.position[1] - 0.5f;
			p2_c1_y = players[1]->transform.position[2] + 0.5f;

			distance = sqrt(pow((ball_pos_x - p2_c1_x),2) + pow((ball_pos_y - p2_c1_y),2));

			if ((distance <= 0.35f) && !hit_corner){
				hit_corner = true;
				ball_vel_y = 8.0f;
				ball_vel_x -= 1.5f;
				ball_vel_x += -1.0f * p2_left;
				ball_vel_x += 1.0f * p2_right;

				p1_touch_last = false;
			}

			p2_c2_x = players[1]->transform.position[1] + 0.5f;
			p2_c2_y = players[1]->transform.position[2] + 0.5f;

			distance = sqrt(pow((ball_pos_x - p2_c2_x),2) + pow((ball_pos_y - p2_c2_y),2));

			if ((distance <= 0.35f) && !hit_corner){
				hit_corner = true;
				ball_vel_y = 8.0f;
				ball_vel_x += 1.5f;
				ball_vel_x += -1.0f * p2_left;
				ball_vel_x += 1.0f * p2_right;

				p1_touch_last = false;
			}

			hit_top = false;

			//if the ball has hit player 1's head, bounce the ball upward
			if ((ball->transform.position[2] <= (players[0]->transform.position[2] + 0.5f)) &&
				(ball->transform.position[2] >= (players[0]->transform.position[2] + 0.25f)) &&
				(ball->transform.position[1] <= (players[0]->transform.position[1] + 0.5f)) &&
				(ball->transform.position[1] >= (players[0]->transform.position[1] - 0.5f)) &&
				!hit_corner && !hit_top){

				ball->transform.position[2] = players[0]->transform.position[2] + 0.85f;
				ball_vel_y = 8.0f;
				ball_vel_x += -1.0f * p1_left;
				ball_vel_x += 1.0f * p1_right;

				p1_touch_last = true;

				hit_top = true;
			}	


			//if the ball has hit player 2's head, bounce the ball upward
			if ((ball->transform.position[2] <= (players[1]->transform.position[2] + 0.5f)) &&
				(ball->transform.position[2] >= (players[1]->transform.position[2] + 0.25f)) &&
				(ball->transform.position[1] <= (players[1]->transform.position[1] + 0.5f)) &&
				(ball->transform.position[1] >= (players[1]->transform.position[1] - 0.5f)) &&
				!hit_corner && !hit_top){

				ball->transform.position[2] = players[1]->transform.position[2] + 0.85f;
				ball_vel_y = 8.0f;
				ball_vel_x += -1.0f * p2_left;
				ball_vel_x += 1.0f * p2_right;

				p1_touch_last = false;

				hit_top = true;
			}

			hit_side = false;

			//if the ball has hit player 1's left wall, bounce the ball to the left
			if ((ball->transform.position[2] <= (players[0]->transform.position[2] + 0.5f)) &&
				(ball->transform.position[2] >= (players[0]->transform.position[2] - 0.5f)) &&
				(ball->transform.position[1] <= (players[0]->transform.position[1] - 0.4f)) &&
				(ball->transform.position[1] >= (players[0]->transform.position[1] - 0.85f)) &&
				!hit_corner && !hit_top && !hit_side){

				ball->transform.position[1] = players[0]->transform.position[1] - 0.85f;

				if (ball_vel_x >= 0.0){
					ball_vel_x *= -1.0f;
				}

				p1_touch_last = true;

				hit_side = true;
			}	

			//if the ball has hit player 1's right wall, bounce the ball to the right
			if ((ball->transform.position[2] <= (players[0]->transform.position[2] + 0.5f)) &&
				(ball->transform.position[2] >= (players[0]->transform.position[2] - 0.5f)) &&
				(ball->transform.position[1] >= (players[0]->transform.position[1] + 0.4f)) &&
				(ball->transform.position[1] <= (players[0]->transform.position[1] + 0.85f)) &&
				!hit_corner && !hit_top && !hit_side){

				ball->transform.position[1] = players[0]->transform.position[1] + 0.85f;
				
				if (ball_vel_x <= 0.0){
					ball_vel_x *= -1.0f;
				}

				p1_touch_last = true;

				hit_side = true;
			}	

			//if the ball has hit player 2's left wall, bounce the ball to the left
			if ((ball->transform.position[2] <= (players[1]->transform.position[2] + 0.5f)) &&
				(ball->transform.position[2] >= (players[1]->transform.position[2] - 0.5f)) &&
				(ball->transform.position[1] <= (players[1]->transform.position[1] - 0.4f)) &&
				(ball->transform.position[1] >= (players[1]->transform.position[1] - 0.85f)) &&
				!hit_corner && !hit_top && !hit_side){

				ball->transform.position[1] = players[1]->transform.position[1] - 0.85f;
				
				if (ball_vel_x >= 0.0){
					ball_vel_x *= -1.0f;
				}

				p1_touch_last = false;

				hit_side = true;
			}

			//if the ball has hit player 2's left wall, bounce the ball to the left
			if ((ball->transform.position[2] <= (players[1]->transform.position[2] + 0.5f)) &&
				(ball->transform.position[2] >= (players[1]->transform.position[2] - 0.5f)) &&
				(ball->transform.position[1] >= (players[1]->transform.position[1] + 0.4f)) &&
				(ball->transform.position[1] <= (players[1]->transform.position[1] + 0.85f)) &&
				!hit_corner && !hit_top && !hit_side){

				ball->transform.position[1] = players[1]->transform.position[1] + 0.85f;
				
				if (ball_vel_x <= 0.0){
					ball_vel_x *= -1.0f;
				}

				p1_touch_last = false;

				hit_side = true;
			}		

			//check if the ball has hit the net

			/*check if the ball hits the top of the net first*/
			//player 1 corners
			net_c_x = net->transform.position[1] - 0.5f;
			net_c_y = net->transform.position[2] + 0.5f;

			distance = sqrt(pow((ball_pos_x - net_c_x),2) + pow((ball_pos_y - net_c_y),2));

			if (distance <= 0.35f){
				ball->transform.position[1] = players[0]->transform.position[1];
				ball->transform.position[2] = 4.0f;
				ball_vel_y = 0.0f;
				ball_vel_x = 0.0f;

				if (p1_touch_last){
					p2_score += 1;
				} else {
					p1_score += 1;
				}

				printf("Current Score: p1 %i | p2 %i\n", p1_score, p2_score);

				if ((p1_score == 10) || (p2_score == 10)){
					printf("GAME OVER: ");
					if (p1_score == 10){
						printf("Player1 wins!\n");
					} else {
						printf("Player2 wins!\n");
					}
					game_over = true;
				}
			}


			//if the ball has hit the net's left wall, reset the ball
			if ((ball->transform.position[2] <= (net->transform.position[2] + 1.0f)) &&
				(ball->transform.position[1] <= (net->transform.position[1] - 0.0f)) &&
				(ball->transform.position[1] >= (net->transform.position[1] - 0.40f))){

				ball->transform.position[1] = players[0]->transform.position[1];
				ball->transform.position[2] = 4.0f;
				ball_vel_y = 0.0f;
				ball_vel_x = 0.0f;

				if (p1_touch_last){
					p2_score += 1;
				} else {
					p1_score += 1;
				}

				printf("Current Score: p1 %i | p2 %i\n", p1_score, p2_score);

				if ((p1_score == 10) || (p2_score == 10)){
					printf("GAME OVER: ");
					if (p1_score == 10){
						printf("Player1 wins!\n");
					} else {
						printf("Player2 wins!\n");
					}
					game_over = true;
				}
			}

			//if the ball has hit the net's left wall, bounce the ball to the left
			if ((ball->transform.position[2] <= (net->transform.position[2] + 1.0f)) &&
				(ball->transform.position[1] >= (net->transform.position[1] + 0.0f)) &&
				(ball->transform.position[1] <= (net->transform.position[1] + 0.40f))){

				ball->transform.position[1] = players[0]->transform.position[1];
				ball->transform.position[2] = 4.0f;
				ball_vel_y = 0.0f;
				ball_vel_x = 0.0f;

				if (p1_touch_last){
					p2_score += 1;
				} else {
					p1_score += 1;
				}

				printf("Current Score: p1 %i | p2 %i\n", p1_score, p2_score);

				if ((p1_score == 10) || (p2_score == 10)){
					printf("GAME OVER: ");
					if (p1_score == 10){
						printf("Player1 wins!\n");
					} else {
						printf("Player2 wins!\n");
					}
					game_over = true;
				}
			}	

			//if the ball reached the floor, reset velocity and z position
			if (ball->transform.position[2] <= 0.35f){
				if (ball->transform.position[1] >= 0){
					p1_score += 1;
				} else {
					p2_score += 1;
				}

				printf("Current Score: p1 %i | p2 %i\n", p1_score, p2_score);

				if ((p1_score == 10) || (p2_score == 10)){
					printf("GAME OVER: ");
					if (p1_score == 10){
						printf("Player1 wins!\n");
					} else {
						printf("Player2 wins!\n");
					}
					game_over = true;
				}

				ball->transform.position[1] = players[0]->transform.position[1];
				ball->transform.position[2] = 4.0f;
				ball_vel_y = 0.0f;
				ball_vel_x = 0.0f;
			}

			//apply gravity to velocities
			//don't apply gravity when the players are on the floor
			if (players[0]->transform.position[2] != 0.5f){
				p1_vel_y += (gravity / 60.0f);
			}
			if (players[1]->transform.position[2] != 0.5f){
				p2_vel_y += (gravity / 60.0f);
			}
			if (!game_over){
				ball_vel_y += (gravity / 60.0f);
			}

			//camera:
			scene.camera.transform.position = camera.radius * glm::vec3(
				std::cos(camera.elevation) * std::cos(camera.azimuth),
				std::cos(camera.elevation) * std::sin(camera.azimuth),
				std::sin(camera.elevation)) + camera.target;

			glm::vec3 out = -glm::normalize(camera.target - scene.camera.transform.position);
			glm::vec3 up = glm::vec3(0.0f, 0.0f, 1.0f);
			up = glm::normalize(up - glm::dot(up, out) * out);
			glm::vec3 right = glm::cross(up, out);
			
			scene.camera.transform.rotation = glm::quat_cast(
				glm::mat3(right, up, out)
			);
			scene.camera.transform.scale = glm::vec3(1.0f, 1.0f, 1.0f);
		}

		//draw output:
		glClearColor(0.5, 0.5, 0.5, 0.0);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
		glEnable(GL_DEPTH_TEST);
		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);


		{ //draw game state:
			glUseProgram(program);
			glUniform3fv(program_to_light, 1, glm::value_ptr(glm::normalize(glm::vec3(0.0f, 1.0f, 10.0f))));
			scene.render();
		}


		SDL_GL_SwapWindow(window);
	}


	//------------  teardown ------------

	SDL_GL_DeleteContext(context);
	context = 0;

	SDL_DestroyWindow(window);
	window = NULL;

	return 0;
}



static GLuint compile_shader(GLenum type, std::string const &source) {
	GLuint shader = glCreateShader(type);
	GLchar const *str = source.c_str();
	GLint length = source.size();
	glShaderSource(shader, 1, &str, &length);
	glCompileShader(shader);
	GLint compile_status = GL_FALSE;
	glGetShaderiv(shader, GL_COMPILE_STATUS, &compile_status);
	if (compile_status != GL_TRUE) {
		std::cerr << "Failed to compile shader." << std::endl;
		GLint info_log_length = 0;
		glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &info_log_length);
		std::vector< GLchar > info_log(info_log_length, 0);
		GLsizei length = 0;
		glGetShaderInfoLog(shader, info_log.size(), &length, &info_log[0]);
		std::cerr << "Info log: " << std::string(info_log.begin(), info_log.begin() + length);
		glDeleteShader(shader);
		throw std::runtime_error("Failed to compile shader.");
	}
	return shader;
}

static GLuint link_program(GLuint fragment_shader, GLuint vertex_shader) {
	GLuint program = glCreateProgram();
	glAttachShader(program, vertex_shader);
	glAttachShader(program, fragment_shader);
	glLinkProgram(program);
	GLint link_status = GL_FALSE;
	glGetProgramiv(program, GL_LINK_STATUS, &link_status);
	if (link_status != GL_TRUE) {
		std::cerr << "Failed to link shader program." << std::endl;
		GLint info_log_length = 0;
		glGetProgramiv(program, GL_INFO_LOG_LENGTH, &info_log_length);
		std::vector< GLchar > info_log(info_log_length, 0);
		GLsizei length = 0;
		glGetProgramInfoLog(program, info_log.size(), &length, &info_log[0]);
		std::cerr << "Info log: " << std::string(info_log.begin(), info_log.begin() + length);
		throw std::runtime_error("Failed to link program");
	}
	return program;
}
