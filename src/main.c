#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <SDL2/SDL.h>
#include <stdlib.h> // Required for rand() and srand()
#include <time.h>   // Required for time()

#include "stack.h"

#define WINDOW_TITLE  "CHIP-8"
#define WINDOW_SCALE  10
#define SCREEN_WIDTH  64
#define SCREEN_HEIGHT 32

#define CPU_HZ         240
#define TIMER_HZ       60
#define CYCLES_PER_FRAME (CPU_HZ / TIMER_HZ) // ~8 instructions per frame


#define MEMORY_SIZE 4096 // bytes
#define DISPLAY_SIZE 2048 // 64 * 32

// Chip8 Configuration
#define SHIFT_MODE 0 // 0 for old, 1 for new (0x8XYE and 0x8XY6)
#define JUMP_MODE 0  // 0 for old, 1 for new (BNNN)
#define INDEX_ADDITION_OVERLFOW 1 // 0 for COSMAC Chip 8, 1 for Amiga

int init_font(uint8_t memory[], int size) {
    (void)size;
    uint8_t font[] = {
        0xF0, 0x90, 0x90, 0x90, 0xF0, // 0
        0x20, 0x60, 0x20, 0x20, 0x70, // 1
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
    memcpy(memory, font, sizeof(font));
    return sizeof(font);
}

int load_rom(FILE* rom, uint8_t memory[]) {
    if (!rom) {
        return -1;
    }
    fread(memory + 0x200, 1, MEMORY_SIZE - 0x200, rom);
    return 1;
}


int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("Usage: %s <rom path>\n", argv[0]);
        return 1;
    }

    uint8_t memory[MEMORY_SIZE];
    bool display[DISPLAY_SIZE];
    memset(display, 0, sizeof(display));
    struct Stack* stack = createStack(16);
    uint16_t pc = 0x200; // program counter — ROMs load at 0x200
    uint16_t I  = 0;     // index register
    uint8_t V[16]; // 1 byte variable registers

    uint8_t delay_timer = 0;
    uint8_t sound_timer = 0;

    int font_size = init_font(memory, MEMORY_SIZE);
    printf("Font Size: %d\n", font_size);

    FILE* rom = fopen(argv[1], "rb");
    int load_rom_result = load_rom(rom, memory);
    if (load_rom_result != 1) {
        printf("Failed to open ROM: %s\n", argv[1]);
        return 0;
    }
    fclose(rom);

    // Keypad
    bool keypad[16] = {0};

    // Maps CHIP-8 key index (0x0-0xF) to SDL scancode
    SDL_Scancode keymap[16] = {
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

    // Initialize SDL
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) != 0) {
        printf("SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    // Create window
    SDL_Window* window = SDL_CreateWindow(
        WINDOW_TITLE,
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        SCREEN_WIDTH * WINDOW_SCALE, SCREEN_HEIGHT * WINDOW_SCALE,
        0
    );
    if (!window) {
        printf("SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    // Create renderer
    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    if (!renderer) {
        printf("SDL_CreateRenderer failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    srand(time(NULL)); // set random seed

    bool running = true;
    SDL_Event event;
    while (running) {
        uint64_t frame_start = SDL_GetTicks64();
        const uint8_t* keys = SDL_GetKeyboardState(NULL);
        for (int k = 0; k < 16; k++) {
            keypad[k] = keys[keymap[k]];
        }
        for (int c = 0; c < CYCLES_PER_FRAME; c++) {
            // Fetch
            uint16_t opcode = (memory[pc] << 8) + memory[pc+1];
            pc += 2;

            // Decode
            uint8_t first_nibble = (opcode & 0xF000) >> 12;
            uint8_t second_nibble = (opcode & 0x0F00) >> 8;
            uint8_t third_nibble = (opcode & 0x00F0) >> 4;
            uint8_t fourth_nibble = (opcode & 0x000F);

            uint8_t  nn  = (opcode & 0x00FF);
            uint16_t nnn = (opcode & 0x0FFF);

            // Execute
            switch (first_nibble) {
                case 0x0:
                    if (opcode == 0x00E0) {
                        // Clear screen
                        memset(display, 0, sizeof(display));
                    }
                    else if  (opcode == 0x00EE) {
                        // Return from subroutine
                        pc = (uint16_t) pop(stack);
                    }
                    break;

                case 0x1:
                    // Jump to addreess
                    pc = nnn;
                    break;

                case 0x2:
                    // Jump to subroutine
                    push(stack, (int) pc);
                    pc = nnn;
                    break;

                case 0x3:
                    // 3XNN
                    // skip one instruction (2 bytes) if VX == NN
                    if (V[second_nibble] == nn) {
                        pc += 2;
                    }
                    break;

                case 0x4:
                    // 4XNN
                    // skip one instruction (2 bytes) if VX != NN
                    if (V[second_nibble] != nn) {
                        pc += 2;
                    }
                    break;

                case 0x5:
                    // 5XY0
                    // skip one instruction (2 byes) if VX == VY
                    if (V[second_nibble] == V[third_nibble]) {
                        pc += 2;
                    }
                    break;

                case 0x9:
                    // 9XY0
                    // skip one instruction (2 byes) if VX != VY
                    if (V[second_nibble] != V[third_nibble]) {
                        pc += 2;
                    }
                    break;

                case 0x6: // set
                    // 6XNN
                    // x in [0,15]
                    V[second_nibble] = nn;
                    break;

                case 0x7: // add
                    // 7XNN
                    V[second_nibble] += nn;
                    break;

                case 0x8:
                    uint8_t X = second_nibble;
                    uint8_t Y = third_nibble;
                    switch (fourth_nibble) {
                        case 0x0: V[X] = V[Y]; break; // 8XY0
                        case 0x1: V[X] = V[X] | V[Y]; break; // 8XY1
                        case 0x2: V[X] = V[X] & V[Y]; break; // 8XY2
                        case 0x3: V[X] = V[X] ^ V[Y]; break; // 8XY3
                        case 0x4:
                        {
                            // 8XY4. Add with carry flag
                            uint16_t result = V[X] + V[Y];
                            // 8bit unsigned max is 255 or 0xFF
                            uint8_t carry = result > 0xFF ? 1 : 0;
                            V[X] = result & 0xFF; //bitmask to ignore everything except lower 8 bits
                            V[0xF] = carry;
                        }
                        break;

                        case 0x5: // 8XY5: VX = VX - VY
                        {
                            uint8_t no_borrow = V[X] >= V[Y] ? 1 : 0;
                            V[X] -= V[Y];
                            V[0xF] = no_borrow;
                            break;
                        }
                        break;

                        case 0x7: // 8XY5: VX = VX - VY
                        {
                            uint8_t no_borrow = V[Y] >= V[X] ? 1 : 0;
                            V[X] -= V[Y];
                            V[0xF] = no_borrow;
                            break;
                        }
                        break;

                        case 0x6: // right shift
                        {
                            // Chip8 Original, Put VY into VX, and then shift
                            // Chip48 and SUPER-CHIP ignores VY completely.
                            if (SHIFT_MODE == 0) {
                                V[X] = V[Y];
                            }
                            V[X] = V[X] >> 1;
                        }
                        break;

                        case 0xE: // left shift
                        {
                            // Chip8 Original, Put VY into VX, and then shift
                            // Chip48 and SUPER-CHIP ignores VY completely.
                            if (SHIFT_MODE == 0) {
                                V[X] = V[Y];
                            }
                            V[X] = V[X] << 1;
                        }
                        break;
                    }
                    break;

                case 0xA:
                    // ANNN
                    // set Index Register to NNN
                    I = nnn;
                    break;

                case 0xB:
                    // BNNN
                    // jump with offset

                    // Original Chip8 was just jump to NNN + V0, but newer ones are like BXNN, where you jump XNN + V[X]
                    if (JUMP_MODE == 0) {
                        pc = nnn + V[0];
                    }
                    else if (JUMP_MODE == 1) {
                        pc = nnn + V[X];
                    }
                    else {
                        printf("Critical Error: JUMP_MODE must be 0 or 1");
                        return -1;
                    }
                    break;
                case 0xC:
                    // CXNN
                    // Random. Generate Random Number, & with NN, set to V[X]
                    int random_num = rand();
                    V[second_nibble] = random_num & nn;
                    break;

                case 0xD:
                    // DXYN
                    // Draw sprite at (VX, VY) with width 8 pixels and height N pixels
                    // Sprite data is read from memory starting at address I
                    // VF is set to 1 if any pixels are flipped from set to unset, and 0 otherwise
                    {
                        uint8_t x = V[second_nibble] & 63; // tbh I have no idea what the and is for
                        uint8_t y = V[third_nibble] & 31;
                        uint8_t height = fourth_nibble;
                        V[0xF] = 0; // Reset collision flag

                        for (int row = 0; row < height; row++) {
                            uint8_t sprite_byte = memory[I + row];
                            for (int col = 0; col < 8; col++) {
                                if ((sprite_byte & (0x80 >> col)) != 0) { // 0x80 looks like 10000000, create a sliding bitmask to check whether pixel at col is on or off
                                    int display_index = ((y + row) % SCREEN_HEIGHT) * SCREEN_WIDTH + ((x + col) % SCREEN_WIDTH);
                                    if (display[display_index]) {
                                        V[0xF] = 1; // Collision detected
                                    }
                                    display[display_index] ^= 1; // XOR pixel
                                }
                            }
                        }
                    }
                    break;

                case 0xE:
                    if (nn == 0x9E) {
                        // EX9E — skip if key VX is pressed
                        if (keypad[V[second_nibble] & 0xF]) {
                            pc += 2;
                        }
                    }
                    else if (nn == 0xA1) {
                        // EXA1 — skip if key VX is NOT pressed
                        if (!keypad[V[second_nibble] & 0xF]) {
                            pc += 2;
                        }
                    }
                break;

                case 0xF:
                    if (nn == 0x07) { // FX07
                        V[second_nibble] = delay_timer;
                    }
                    else if (nn == 0x15) { // FX15
                        delay_timer = V[second_nibble];
                    }
                    else if (nn == 0x18) { // FX18
                        sound_timer = V[second_nibble];
                    }
                    else if (nn == 0x1E) { // FX1E
                        if (INDEX_ADDITION_OVERLFOW == 0) {
                            I += V[second_nibble];
                        }
                        else if (INDEX_ADDITION_OVERLFOW == 1) {
                            uint32_t result = I + V[second_nibble];
                            uint8_t carry = result > 0xFFFF? 1:0;
                            I = result & 0xFFFF; // bitmask to map 32 bit int to 16 bit

                        }

                    }
                    else if (nn == 0x0A) { // FXA0
                        bool key_pressed = false;
                        for (int i=0; i<16;i++) {
                            if (keypad[i] == true) {
                                key_pressed = true;
                                V[second_nibble] = i;
                                break;
                            }
                        }
                        if (!key_pressed) {
                            pc -= 2;
                        }
                    }
                    else if (nn == 0x29) { // FX29
                        // Characters 0-F (in hexadecimal) are represented by a 4x5 font
                        // Font data is stored in the first 80 bytes of memory (0x000 to 0x050)
                        // Each character is 5 bytes long, so I = V[X] * 5
                        I = V[second_nibble] * 5;
                    }
                    else if (nn == 0x33) {
                        uint8_t v_x = V[second_nibble];
                        uint8_t ones = v_x % 10;
                        uint8_t tens = ((v_x - ones) / 10) % 10;
                        uint8_t hundreds = (v_x - ((10*tens) + ones)) / 100;
                        memory[I] = hundreds;
                        memory[I+1] = tens;
                        memory[I+2] = ones;
                    }

                    // old systems increased I, but I can't be bothered
                    else if (nn == 0x55) { // FX55
                        int x = (int) second_nibble;
                        for (int j=0; j<=x;j++) {
                            memory[I+j] = V[j];
                        }
                    }
                    else if (nn == 0x65) { // FX65
                        int x = (int) second_nibble;
                        for (int j=0; j<=x;j++) {
                            V[j] = memory[I+j];
                        }
                    }
                break;

                default:
                    printf("Unknown opcode: 0x%04X at PC: 0x%04X\n", opcode, pc - 2);
                    break;
            }
        }

        // Decrement timers once per frame (60Hz)
        if (delay_timer > 0) delay_timer--;
        if (sound_timer > 0) sound_timer--;

        // Handle Events
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                running = false;
            }
        }

        // Render
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);

        SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
        for (int y = 0; y < SCREEN_HEIGHT; y++) {
            for (int x = 0; x < SCREEN_WIDTH; x++) {
                if (display[y * SCREEN_WIDTH + x]) {
                    SDL_Rect pixel = {
                        x * WINDOW_SCALE,
                        y * WINDOW_SCALE,
                        WINDOW_SCALE,
                        WINDOW_SCALE
                    };
                    SDL_RenderFillRect(renderer, &pixel);
                }
            }
        }
        SDL_RenderPresent(renderer);
    }
    // Cleanup
    freeStack(stack);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
