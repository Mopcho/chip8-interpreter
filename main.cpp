#include <fstream>
#include <iostream>
#include <vector>
#define SDL_MAIN_USE_CALLBACKS 1
#include <iomanip>
#include <random>
#include <thread>
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <SDL3/SDL_audio.h>
#include <numbers>

#define DISPLAY_SCALE 10
#define DISPLAY_WIDTH (64 * DISPLAY_SCALE)
#define DISPLAY_HEIGHT (32 * DISPLAY_SCALE)
#define ROM_START 0x200

constexpr int TIMER_HZ = 60;
constexpr int CPU_HZ = 500;

std::vector<uint8_t> readFile(std::string_view path)
{
    std::ifstream file(path.data(), std::ios::binary | std::ios::ate);
    if (!file)
    {
        throw std::runtime_error("Cannot open file");
    }

    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);

    std::vector<uint8_t> buffer(size);

    if (!file.read(reinterpret_cast<char*>(buffer.data()), size))
    {
        throw std::runtime_error("Failed to read file");
    }

    return buffer;
}

uint8_t fonts[80]
{
    0xF0, 0x90, 0x90, 0x90, 0xF0, // 0
    0x20, 0x20, 0x60, 0x20, 0x70, // 1
    0xF0, 0x10, 0xF0, 0x80, 0xF0, // 2
    0xF0, 0x10, 0xF0, 0x10, 0xF0, // 3
    0x90, 0x90, 0xF0, 0x10, 0x10, // 4
    0xF0, 0x80, 0xF0, 0x10, 0xF0, // 5
    0xF0, 0x80, 0xF0, 0x90, 0xF0, // 6
    0xF0, 0x10, 0x20, 0x40, 0x40, // 7
    0xF0, 0x90, 0xF0, 0x90, 0xF0, // 8
    0xF0, 0x90, 0xF0, 0x10, 0xF0, // 9
    0xF0, 0x90, 0xF0, 0x90, 0x90, // A
    0xE0, 0x90, 0xE0, 0x90, 0xE0, // B
    0xF0, 0x80, 0x80, 0x80, 0xF0, // C
    0xE0, 0x90, 0x90, 0x90, 0xE0, // D
    0xF0, 0x80, 0xF0, 0x80, 0xF0, // E
    0xF0, 0x80, 0xF0, 0x80, 0x80  // F
};

int keymap[16] = {
    SDL_SCANCODE_X,    // 0
    SDL_SCANCODE_1,    // 1
    SDL_SCANCODE_2,    // 2
    SDL_SCANCODE_3,    // 3
    SDL_SCANCODE_Q,    // 4
    SDL_SCANCODE_W,    // 5
    SDL_SCANCODE_E,    // 6
    SDL_SCANCODE_A,    // 7
    SDL_SCANCODE_S,    // 8
    SDL_SCANCODE_D,    // 9
    SDL_SCANCODE_Z,    // A
    SDL_SCANCODE_C,    // B
    SDL_SCANCODE_4,    // C
    SDL_SCANCODE_R,    // D
    SDL_SCANCODE_F,    // E
    SDL_SCANCODE_V     // F
};

static SDL_Window* windows = nullptr;
static SDL_Renderer* renderer = nullptr;;
static SDL_AudioStream *stream = nullptr;
const bool* keyboard;
static std::mt19937 rng(std::random_device{}());

struct chip8
{
    uint8_t memory[4096]{};
    uint8_t V[16]{};
    uint16_t I = 0;
    uint16_t pc = 0x200;
    uint16_t stack[16]{};
    uint8_t sp = 0;

    uint8_t delay_timer{0};
    uint8_t sound_timer{0};
};

chip8* chip = nullptr;
uint8_t gfx[64 * 32];
uint32_t pixels[64 * 32];
SDL_Texture* texture = nullptr;
int draw_flag = 0;
uint32_t last_timer = SDL_GetTicks();
uint32_t last_cycle = SDL_GetTicks();

void setup_chip(std::string_view rom_file)
{
    chip = new chip8;

    memcpy(&chip->memory[0x0], fonts, 80);

    std::vector<uint8_t> rom_content = readFile(rom_file);
    if (rom_content.size() > sizeof(chip->memory) - ROM_START)
    {
        throw std::runtime_error("ROM too large");
    }
    memcpy(&chip->memory[ROM_START], rom_content.data(), rom_content.size());
}

void print_chip_memory()
{
    uint16_t pc = ROM_START;
    uint16_t end = 0xFFF;
    std::cout << std::hex << std::setfill('0');
    while (pc + 1 < end)
    {
        uint16_t opcode =
            (static_cast<uint8_t>(chip->memory[pc]) << 8) |
            static_cast<uint8_t>(chip->memory[pc + 1]);

        std::cout << std::setw(4) << opcode << " | ";
        pc += 2;
    }

    std::cout << std::dec << '\n';
}

void emulate_cycle()
{
    uint16_t opcode = (chip->memory[chip->pc] << 8) | chip->memory[chip->pc + 1];

    std::cout << std::hex << opcode << std::endl;

    switch (opcode & 0xF000)
    {
    case 0x0000:
        {
            switch (opcode & 0x00FF)
            {
            case 0x00E0:
                {
                    memset(gfx, 0, sizeof(gfx));
                    draw_flag = 1;
                    break;
                }
            case 0x00EE:
                {
                    chip->pc = chip->stack[chip->sp];
                    chip->sp--;
                    return;
                }
            default:
                {
                    std::cout << std::hex << opcode << std::endl;
                    break;
                }
            }
        }
        break;
    case 0x1000:
        chip->pc = opcode & 0x0FFF;
        return;
    case 0x2000:
        {
            chip->sp++;
            chip->stack[chip->sp] = chip->pc + 2;
            chip->pc = (opcode & 0x0FFF);
            return;
        }
    case 0x3000:
        {
            uint8_t X = (opcode & 0x0F00) >> 8;
            uint16_t NN = opcode & 0x00FF;
            if (chip->V[X] == NN)
            {
                chip->pc += 2;
            }
            break;
        }
    case 0x4000:
        {
            uint8_t X = (opcode & 0x0F00) >> 8;
            uint16_t NN = opcode & 0x00FF;
            if (chip->V[X] != NN)
            {
                chip->pc += 2;
            }
            break;
        }
    case 0x5000:
        {
            uint8_t X = (opcode & 0x0F00) >> 8;
            uint8_t Y = (opcode & 0x00F0) >> 4;
            if (chip->V[X] == chip->V[Y])
            {
                chip->pc += 2;
            }
            break;
        }
    case 0x6000:
        {
            uint8_t X = (opcode & 0x0F00) >> 8;
            chip->V[X] = opcode & 0x00FF;
            break;
        }
    case 0x7000:
        {
            uint8_t X = (opcode & 0x0F00) >> 8;
            chip->V[X] += opcode & 0x00FF;
            break;
        }
    case 0x8000:
        {
            uint16_t X = (opcode & 0x0F00) >> 8;
            uint16_t Y = (opcode & 0x00F0) >> 4;

            switch (opcode & 0x000F)
            {
            case 0x0:
                {
                    chip->V[X] = chip->V[Y];
                    break;
                }
            case 0x1:
                {
                    chip->V[X] |= chip->V[Y];
                    break;
                }
            case 0x2:
                {
                    chip->V[X] &= chip->V[Y];
                    break;
                }
            case 0x3:
                {
                    chip->V[X] ^= chip->V[Y];
                    break;
                }
            case 0x4:
                {
                    uint16_t sum = chip->V[X] + chip->V[Y];
                    chip->V[0xF] = (sum > 0xFF);
                    chip->V[X] = sum & 0xFF;
                    break;
                }
            case 0x5:
                {
                    chip->V[0xF] = (chip->V[X] >= chip->V[Y]);
                    chip->V[X] -= chip->V[Y];
                    break;
                }
            case 0x6:
                {
                    chip->V[0xF] = chip->V[X] & 0x1;
                    chip->V[X] >>= 1;
                    break;
                }
            case 0x7:
                {
                    chip->V[0xF] = chip->V[Y] >= chip->V[X];
                    chip->V[X] = chip->V[Y] - chip->V[X];
                    break;
                }
            case 0xE:
                {
                    chip->V[0xF] = (chip->V[X] & 0x80) >> 7;
                    chip->V[X] <<= 1;
                    break;
                }
            }
            break;
        }
    case 0x9000:
        {
            uint8_t X = (opcode & 0x0F00) >> 8;
            uint8_t Y = (opcode & 0x00F0) >> 4;
            if (chip->V[X] != chip->V[Y])
            {
                chip->pc += 2;
            }
            break;
        }
    case 0xA000:
        {
            chip->I = (opcode & 0x0FFF);
            break;
        }
    case 0xB000:
        {
            uint16_t NNN = opcode & 0x0FFF;
            chip->pc = NNN + chip->V[0];
            return;
        }
    case 0xC000:
        {
            std::uniform_int_distribution<std::mt19937::result_type> dist(0, 255);
            uint16_t NN = opcode & 0x00FF;
            uint8_t X = (opcode & 0x0F00) >> 8;
            chip->V[X] = dist(rng) & NN;
            break;
        }
    case 0xD000:
        {
            uint8_t Vx = (opcode & 0x0F00) >> 8;
            uint8_t Vy = (opcode & 0x00F0) >> 4;
            uint8_t N = opcode & 0x000F;

            chip->V[0xF] = 0;

            for (int row = 0; row < N; row++)
            {
                uint8_t sprite = chip->memory[chip->I + row];
                for (int col = 0; col < 8; col++)
                {
                    if ((sprite & (0x80 >> col)) != 0)
                    {
                        int x = (chip->V[Vx] + col) % 64;
                        int y = (chip->V[Vy] + row) % 32;
                        int idx = x + (y * 64);

                        if (gfx[idx] == 1)
                        {
                            chip->V[0xF] = 1;
                        }

                        gfx[idx] ^= 1;
                    }
                }
            }

            draw_flag = 1;
            break;
        }
    case 0xE000:
        {
            uint8_t X = (opcode & 0x0F00) >> 8;
            switch (opcode & 0x00FF)
            {
            case 0x009E:
                {
                    if (keyboard[keymap[chip->V[X]]])
                    {
                        chip->pc += 2;
                    }
                    break;
                }
            case 0x00A1:
                {
                    if (!keyboard[keymap[chip->V[X]]])
                    {
                        chip->pc += 2;
                    }
                    break;
                }
            }
            break;
        }
    case 0xF000:
        {
            switch (opcode & 0x00FF)
            {
            case 0x000A:
                {
                    bool pressed = false;
                    for (int i = 0; i < 16; i++)
                    {
                        if (keyboard[keymap[i]])
                        {
                            chip->V[(opcode & 0x0F00) >> 8] = i;
                            pressed = true;
                            break;
                        }
                    }
                    if (!pressed)
                    {
                        return;
                    }
                    break;
                }
            case 0x0007:
                {
                    uint8_t X = (opcode & 0x0F00) >> 8;
                    chip->V[X] = chip->delay_timer;
                    break;
                }
            case 0x0015:
                {
                    chip->delay_timer = chip->V[(opcode & 0x0F00) >> 8];
                    break;
                }
            case 0x0018:
                {
                    chip->sound_timer = chip->V[(opcode & 0x0F00) >> 8];
                    break;
                }
            case 0x001E:
                {
                    chip->I += chip->V[(opcode & 0x0F00) >> 8];
                    break;
                }
            case 0x0029:
                {
                    uint8_t Vx = (opcode & 0x0F00) >> 8;
                    chip->I = chip->V[Vx] * 5;
                    break;
                }
            case 0x0033:
                {
                    uint8_t X = (opcode & 0x0F00) >> 8;
                    uint8_t value = chip->V[X];
                    chip->memory[chip->I] = value / 100;
                    chip->memory[chip->I + 1] = (value / 10) % 10;
                    chip->memory[chip->I + 2] = value % 10;
                    break;
                }
            case 0x55:
                {
                    uint8_t X = (opcode & 0x0F00) >> 8;
                    for (uint8_t i = 0; i <= X; i++)
                        chip->memory[chip->I + i] = chip->V[i];
                    break;
                }
            case 0x65:
                {
                    uint8_t X = (opcode & 0x0F00) >> 8;
                    for (uint8_t i = 0; i <= X; i++)
                        chip->V[i] = chip->memory[chip->I + i];
                    break;
                }
            }
            break;
        }
    default:
        {
            std::cout << std::hex << opcode << std::endl;
            break;
        }
    }

    chip->pc += 2;
}

SDL_AppResult SDL_AppInit(void** appstate, int argc, char* argv[])
{
    if (argc < 2)
    {
        throw std::exception("Invalid arguments count");
    }
    if (!SDL_Init(SDL_INIT_VIDEO))
    {
        SDL_Log("Cpouldnt initialize SDL");
        return SDL_APP_FAILURE;
    }
    if (!SDL_Init(SDL_INIT_AUDIO)) {
        std::cerr << "SDL_Init error: " << SDL_GetError() << "\n";
        return SDL_APP_FAILURE;
    }
    if (!SDL_CreateWindowAndRenderer("CHIP-8", DISPLAY_WIDTH, DISPLAY_HEIGHT, 0x0, &windows, &renderer))
    {
        SDL_Log("Cpouldnt initialize window and renderer %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }
    SDL_SetRenderLogicalPresentation(renderer, DISPLAY_WIDTH, DISPLAY_HEIGHT, SDL_LOGICAL_PRESENTATION_LETTERBOX);

    texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_STREAMING, 64, 32);

    SDL_AudioSpec spec;
    spec.freq = 48000;
    spec.format = SDL_AUDIO_S16;
    spec.channels = 1;

    stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
    if (!stream)
    {
        SDL_Log("Couldn't create audio stream: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }
    SDL_ResumeAudioStreamDevice(stream);

    setup_chip(argv[1]);
    print_chip_memory();

    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppEvent(void* appstate, SDL_Event* event)
{
    if (event->type == SDL_EVENT_QUIT)
    {
        return SDL_APP_SUCCESS;
    }
    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppIterate(void* appstate)
{
    SDL_PumpEvents();
    keyboard = SDL_GetKeyboardState(nullptr);

    uint64_t now = SDL_GetTicks();

    if (now - last_cycle >= 1000 / CPU_HZ)
    {
        emulate_cycle();
        last_cycle = now;
    }

    if (now - last_timer >= 1000 / TIMER_HZ)
    {
        if (chip->delay_timer > 0) chip->delay_timer--;
        if (chip->sound_timer > 0) chip->sound_timer--;

        last_timer = now;
    }

    for (int i = 0; i < 64 * 32; i++)
    {
        pixels[i] = gfx[i] ? 0xFFFFFFFF : 0x00000000;
    }
    if (draw_flag)
    {
        SDL_UpdateTexture(texture, nullptr, pixels, 64 * sizeof(uint32_t));
        SDL_RenderClear(renderer);
        SDL_RenderTexture(renderer, texture, nullptr, nullptr);
        SDL_RenderPresent(renderer);

        draw_flag = 0;
    }

    return SDL_APP_CONTINUE;
}

void SDL_AppQuit(void* appstate, SDL_AppResult result)
{
    delete chip;
}
