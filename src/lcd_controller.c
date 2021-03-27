/*
 * Gemu - Nintendo Game Boy Emulator
 * Copyright (C) 2017  Alex Dempsey

 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.

 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see http://www.gnu.org/licenses/
 *
 */

#include <SDL2\SDL.h>

#include <stdio.h>
#include <stdlib.h>

#include "cpu.h"
#include "memory.h"
#include "lcd_controller.h"
#include "interrupts.h"

#define MODE_0 204
#define MODE_2 80
#define MODE_3 172
#define TOTAL 456

extern SDL_Window* window;
extern SDL_Surface* screen_surface;
extern SDL_Texture* texture;
extern SDL_Renderer* renderer;

u_int8 screen[160][144][3] = {};
u_int8 pixels[160*144*3];
int interrupt_cycles[4] = {MODE_0, MODE_2, MODE_3, TOTAL};
u_int8 sprites[40][4];

void update_graphics()
{
    // if background is enabled draw tiles otherwise
    // draw blank line
    memory[0xff40] & 0x1 ? draw_tiles() : draw_blank();
    // if sprites are enabled draw sprites
    if (memory[0xff40] & 0x2) {
        draw_sprites();
    }
}

void draw_blank()
{
    int ly = memory[0xff44];
    for (int pixel = 0; pixel < 160; ++pixel) {
        screen[pixel][ly][0] = 0xFF;
        screen[pixel][ly][1] = 0xFF;
        screen[pixel][ly][2] = 0xFF;
    }
}

void draw_tiles()
{
    u_int8 scrolly = memory[0xff42];
    u_int8 scrollx = memory[0xff43];
    int winy = memory[0xff4a];
    int winx = memory[0xff4b] - 7;
    int using_window = 0;
    int unsign = 1;
    u_int16 bg_map;
    u_int16 win_map;
    u_int16 tile_data;
    u_int8 ly = memory[0xff44];
    u_int8 lcdc = memory[0xff40];

    // check if window is enabled and the current scanline
    // falls on the window
    if (is_set(lcdc,5) && winy <= ly) {
         using_window = 1;
    }

    // set window tile map
    if (using_window) {
        win_map = is_set(lcdc,6) ? 0x9C00 : 0x9800;
    }

    // set background tile map
    bg_map = is_set(lcdc,3) ? 0x9C00 : 0x9800;

    // set tile data address
    if (is_set(lcdc,4)) {
        tile_data = 0x8000;
    } else {
        tile_data = 0x8800;
        unsign = 0;
    }

    u_int8 y_pos, x_pos;
    u_int16 tile_id, tile_address, tile_location;

    for (int pixel = 0; pixel < 160; ++pixel) {

        // check if current pixel falls on the window
        if (using_window && pixel >= winx) {
            x_pos = pixel - winx;
            y_pos = ly - winy;
            tile_address = win_map + tile_id;
        } else {
            x_pos = pixel + scrollx;
            y_pos = scrolly + ly;
            tile_address = bg_map + tile_id;
        }

        tile_id = ((y_pos / 8 * 32) + (x_pos / 8));
        short tile_num;

        // using tile numbers 0-255
        if (unsign) {
            tile_num = (u_int8)memory[tile_address];
        // using tile numbers -128-127
        } else {
            tile_num = (signed char)memory[tile_address];
        }

        tile_location = tile_data;

        if (unsign) {
            tile_location += tile_num * 16;
        } else {
            tile_location += (tile_num + 128) * 16;
        }

        // get current line and their bytes from memory
        u_int8 line = y_pos % 8;
        line *= 2;
        u_int8 byte1 = memory[tile_location + line];
        u_int8 byte2 = memory[tile_location + line + 1];

        // bit 7 is leftmost pixel
        int bit = x_pos % 8;
        bit -= 7;
        bit *= -1;

        // colour number is bit of first byte and second byte
        int colour_number1 = (byte1 >> bit) & 1;
        int colour_number2 = (byte2 >> bit) & 1;
        int colour_number = (colour_number2 << 1) | colour_number1;

        int red = 0;
        int green = 0;
        int blue = 0;

        u_int8 pallete = memory[0xff47];
        int colour_id;

        // get correct colour from pallete
        switch (colour_number) {
            case 0: colour_id = pallete & 0x03; break;
            case 1: colour_id = (pallete & 0x0C) >> 2; break;
            case 2: colour_id = (pallete & 0x30) >> 4; break;
            case 3: colour_id = (pallete & 0xC0) >> 6; break;
        }

        char shade;

        // shade for current pixel
        if (colour_id == 0) {
            shade = 'W';
        } else if (colour_id == 1) {
            shade = 'L';
        } else if (colour_id == 2) {
            shade = 'D';
        } else {
            shade = 'B';
        }

        switch (shade) {
            case 'W': red = 0xFF; blue = 0xFF; green = 0xFF; break;
            case 'L': red = 0xCC; blue = 0xCC; green = 0xCC; break;
            case 'D': red = 0x77; blue = 0x77; green = 0x77; break;
            default: break;
        }

        if (ly < 0 || ly > 143) {
            continue;
        }
        // set pixel value
        screen[pixel][ly][0] = red;
        screen[pixel][ly][1] = green;
        screen[pixel][ly][2] = blue;
    }
}

void draw_sprites()
{
    u_int8 ly = memory[0xff44];
    u_int8 lcdc = memory[0xff40];
    u_int8 sprites_to_draw[10][4] = {{0}};
    int sprite_size = 8;
    int count = 0;

    // get all sprites from oam memory
    for (int i = 0; i < 40; ++i) {
        for (int j = 0; j < 4; ++j) {
            sprites[i][j] = memory[0xFE00+count];
            ++count;
        }
    }
    int sprite_count = 0;

    if (is_set(lcdc,2)) {
        sprite_size = 16;
    }
    // determine which sprites fall on the current scanline
    if (sprite_size == 8) {
        for (int i = 0; i < 40; ++i) {
            if (ly >= sprites[i][0] - 16 && ly < sprites[i][0] - 8) {
                sprites_to_draw[sprite_count][0] = sprites[i][0];
                sprites_to_draw[sprite_count][1] = sprites[i][1];
                sprites_to_draw[sprite_count][2] = sprites[i][2];
                sprites_to_draw[sprite_count][3] = sprites[i][3];
                ++sprite_count;
                if (sprite_count == 10) {
                    break;
                }
            }
        }
    } else {
        for (int i = 0; i < 40; ++i) {
            if (ly >= sprites[i][0] - 16 && ly < sprites[i][0]) {
                sprites_to_draw[sprite_count][0] = sprites[i][0];
                sprites_to_draw[sprite_count][1] = sprites[i][1];
                sprites_to_draw[sprite_count][2] = sprites[i][2];
                sprites_to_draw[sprite_count][3] = sprites[i][3];
                ++sprite_count;
                if (sprite_count == 10) {
                    break;
                }
            }
        }
    }

    u_int8 tile_number, x_pos, y_pos, attributes;
    int pallete_num;
    u_int8 pallete;
    int colour_number, colour_number1, colour_number2;

    struct sprite spr[sprite_count];

    for (int pixel = 0; pixel < 160; ++pixel) {
        int i;

        // determine if any sprites fall on the current pixel
        for (i = 0; i < sprite_count; ++i) {
            if (pixel >= sprites_to_draw[i][1] - 8 && pixel < sprites_to_draw[i][1]) {
                break;
            }
        }
        if (i == sprite_count) {
            continue;
        }
        count = 0;
        // get all sprites which fall on the current pixel
        for (i = 0; i < sprite_count; ++i) {
            if (pixel >= sprites_to_draw[i][1] - 8 && pixel < sprites_to_draw[i][1]) {
                spr[count].tile_number = sprites_to_draw[i][2];
                spr[count].x_pos = sprites_to_draw[i][1];
                spr[count].y_pos = sprites_to_draw[i][0];
                spr[count].attributes = sprites_to_draw[i][3];
                ++count;
            }
        }
        sort_sprites(spr,count); // sort sprites by x_pos
        u_int16 tile_address_top, tile_address_bottom, tile_address;
        int line, bit;
        u_int8 byte1, byte2;

        // go through each sprite on the current pixel
        for (int i = 0; i < count; ++i) {
            y_pos = spr[i].y_pos;
            x_pos = spr[i].x_pos;
            tile_number = spr[i].tile_number;
            attributes = spr[i].attributes;

            // if sprite size is 8x8
            if (sprite_size == 8) {
                tile_address = 0x8000 + tile_number * 16;
                if (is_set(attributes,6)) {
                    line = (y_pos - 8) - ly - 1;
                } else {
                    line = (y_pos - ly - 16) * -1;
                }
                line *= 2;
                byte1 = memory[tile_address + line];
                byte2 = memory[tile_address + line + 1];
            // sprite size is 8x16
            } else {
                tile_address_top = 0x8000 + (tile_number & 0xFE) * 16;
                tile_address_bottom = 0x8000 + (tile_number | 0x01) * 16;
                // if scanline falls on the bottom tile
                if (ly >= (y_pos - 8)) {
                    // check if the sprite is flipped vertically
                    if (is_set(attributes,6)) {
                        line = y_pos - ly - 1;
                        line *= 2;
                        byte1 = memory[tile_address_top + line];
                        byte2 = memory[tile_address_top + line + 1];
                    } else {
                        line = (y_pos - ly - 8) * -1;
                        line *= 2;
                        byte1 = memory[tile_address_bottom + line];
                        byte2 = memory[tile_address_bottom + line + 1];
                    }
                // scanline falls on the top tile
                } else {
                    // check if sprite is flipped vertically
                    if (is_set(attributes,6)) {
                        line = y_pos - 8 - ly - 1;
                        line *= 2;
                        byte1 = memory[tile_address_bottom + line];
                        byte2 = memory[tile_address_bottom + line + 1];
                    } else {
                        line = (y_pos - 16 - memory[0xff44]) * -1;
                        line *= 2;
                        byte1 = memory[tile_address_top + line];
                        byte2 = memory[tile_address_top + line + 1];
                    }
                }
            }
            // check if sprite is flipped horizontally
            if (is_set(attributes,5)) {
                bit = (x_pos - pixel - 8) * -1;
            } else {
                bit = (x_pos - pixel - 1);
            }

            int priority = is_set(attributes,7);
            pallete_num = is_set(attributes,4);

            if (pallete_num) {
                pallete = memory[0xff49];
            } else {
                pallete = memory[0xff48];
            }

            colour_number1 = (byte1 >> bit) & 1;
            colour_number2 = (byte2 >> bit) & 1;
            colour_number = (colour_number2 << 1) | colour_number1;

            int red = 0;
            int green = 0;
            int blue = 0;

            int colour_id;

            switch (colour_number) {
                case 0: continue;
                case 1: colour_id = (pallete & 0x0C) >> 2; break;
                case 2: colour_id = (pallete & 0x30) >> 4; break;
                case 3: colour_id = (pallete & 0xC0) >> 6; break;
            }

            char shade;

            if (colour_id == 0) {
                shade = 'W';
            } else if (colour_id == 1) {
                shade = 'L';
            } else if (colour_id == 2) {
                shade = 'D';
            } else {
                shade = 'B';
            }
            switch (shade) {
                case 'W': red = 0xFF; blue = 0xFF; green = 0xFF; break;
                case 'L': red = 0xCC; blue = 0xCC; green = 0xCC; break;
                case 'D': red = 0x77; blue = 0x77; green = 0x77; break;
                default: break;
            }

            if (ly < 0 || ly > 143) {
                continue;
            }
            // determines whether sprite is behind the background and changes
            // colours accordingly
            if (priority) {
                u_int8 bg_pallete = memory[0xff47];
                int pixel_col = screen[pixel][ly][0];
                int colour_id1 = (bg_pallete & 0x0C) >> 2;
                int colour_id2 = (bg_pallete & 0x30) >> 4;
                int colour_id3 = (bg_pallete & 0xC0) >> 6;
                if (pixel_col == get_bg_colour(colour_id1) || pixel_col == get_bg_colour(colour_id2) ||
                    pixel_col == get_bg_colour(colour_id3)) {
                    break;
                }
            }
            // set pixel value
            screen[pixel][ly][0] = red;
            screen[pixel][ly][1] = green;
            screen[pixel][ly][2] = blue;
            break;
        }
    }
}

int get_bg_colour(int colour_id)
{
    switch (colour_id) {
        case 0: return 0xFF;
        case 1: return 0xCC;
        case 2: return 0x77;
        default: return 0;
    }
}

// sort sprites according to x values.
// sprites closer to the left will have higher priority
// and appear above the others.
void sort_sprites(struct sprite spr[], int n)
{
    struct sprite temp;
    int j, k;
    for (j = 0; j < n - 1; ++j) {
        for (k = j + 1; k < n; ++k) {
            if (spr[k].x_pos < spr[j].x_pos) {
                temp = spr[j];
                spr[j] = spr[k];
                spr[k] = temp;
            }
        }
    }
}

// renders the next frame
void draw_frame()
{
    int count = 0;
    // loop through and draw each pixel
    for (int i = 0; i < 144; ++i) {
        for (int j = 0; j < 160; ++j) {
            pixels[count++] = screen[j][i][0];
            pixels[count++] = screen[j][i][1];
            pixels[count++] = screen[j][i][2];
        }
    }
    screen_surface = SDL_CreateRGBSurfaceFrom((void*)pixels,160,144,24,160*3,0,0,0,0);
    texture = SDL_CreateTextureFromSurface(renderer,screen_surface);
    SDL_RenderClear(renderer);
    SDL_RenderCopy(renderer, texture, NULL, NULL);
    SDL_RenderPresent(renderer);
    SDL_FreeSurface(screen_surface);
    SDL_DestroyTexture(texture);
}

// switch current lcd mode
void switch_mode(int mode)
{
    memory[0xff41] &= 0xfc;
    switch (mode) {
        case 0: break;
        case 1: memory[0xff41] |= 1; break;
        case 2: memory[0xff41] |= 2; break;
        case 3: memory[0xff41] |= 3; break;
    }
}

// compare lyc and ly registers
void compare_ly()
{
    if (memory[0xff45] == memory[0xff44]) {
        memory[0xff41] |= (1 << 2);
        if (is_set(memory[0xff41],6)) {
            request_interrupt(1);
        }
    } else {
        memory[0xff41] &= ~(1 << 2);
    }
}

// increments ly register
void inc_ly()
{
    ++memory[0xff44];
    if ((memory[0xff44]) == 154) {
        memory[0xff44] = 0;
    }
}

int get_mode()
{
    return memory[0xff41] & 0x03;
}

void update_lcd(int cycles)
{
    // reset lcd values if screen is disabled
    if (!(is_set(memory[0xff40],7))) {
        interrupt_cycles[3] = TOTAL;
        interrupt_cycles[0] = MODE_0;
        interrupt_cycles[1] = MODE_2;
        interrupt_cycles[2] = MODE_3;
        memory[0xff44] = 0;
        memory[0xff41] &= 0xf8;
        return;
    }

    int mode = get_mode();

    // currently in mode 2
    if (mode == 2) {
        interrupt_cycles[1] -= cycles;
        if (interrupt_cycles[1] <= 0) {
            interrupt_cycles[2] += interrupt_cycles[1];
            interrupt_cycles[1] = MODE_2;
            switch_mode(3);
        }
    // currently in mode 3
    } else if (mode == 3) {
        interrupt_cycles[2] -= cycles;
        if (interrupt_cycles[2] <= 0) {
            interrupt_cycles[0] += interrupt_cycles[2];
            interrupt_cycles[2] = MODE_3;
            switch_mode(0);
            if (memory[0xff44] < 144) {
                update_graphics();
            }
            if (is_set(memory[0xff41],3)) {
                request_interrupt(1);
            }
        }
    // currently in mode 0
    } else if (mode == 0) {
        interrupt_cycles[0] -= cycles;
        if (interrupt_cycles[0] <= 0) {
            if (memory[0xff44] < 144) {
                interrupt_cycles[1] += interrupt_cycles[0];
                interrupt_cycles[0] = MODE_0;
                switch_mode(2);
                if (is_set(memory[0xff41],5)) {
                    request_interrupt(1);
                }
            }
        }
    // currently in vblank ie mode 1
    } else {
        if (memory[0xff44] == 0) {
            switch_mode(2);
            if (is_set(memory[0xff41],5)) {
                request_interrupt(1);
            }
        }
    }

    interrupt_cycles[3] -= cycles;

    // increments ly every cycle through all modes
    if (interrupt_cycles[3] <= 0) {
        interrupt_cycles[3] += TOTAL;
        compare_ly();
        inc_ly();
    }
    // last scanline so switch to mode 1 and generate
    // vblank interrupt
    if (memory[0xff44] == 144) {
        if (mode != 1) {
            interrupt_cycles[0] = MODE_0;
            switch_mode(1);
            if (save_request) {
                save_state();
            }
            if (load_request) {
                load_state();
            }
            request_interrupt(0);
            if (is_set(memory[0xff41],4)) {
                request_interrupt(1);
            }
        }
    }
}