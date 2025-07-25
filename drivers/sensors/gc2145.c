/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright (C) 2013-2024 OpenMV, LLC.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 * GC2145 driver.
 */
#include "omv_boardconfig.h"
#if (OMV_GC2145_ENABLE == 1)

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "omv_i2c.h"
#include "omv_csi.h"
#include "gc2145.h"
#include "py/mphal.h"

#define BLANK_LINES             16
#define DUMMY_LINES             16

#define BLANK_COLUMNS           0
#define DUMMY_COLUMNS           8

#define SENSOR_WIDTH            1616
#define SENSOR_HEIGHT           1248

#define ACTIVE_SENSOR_WIDTH     (SENSOR_WIDTH - BLANK_COLUMNS - (2 * DUMMY_COLUMNS))
#define ACTIVE_SENSOR_HEIGHT    (SENSOR_HEIGHT - BLANK_LINES - (2 * DUMMY_LINES))

#define DUMMY_WIDTH_BUFFER      16
#define DUMMY_HEIGHT_BUFFER     8

static int16_t readout_x = 0;
static int16_t readout_y = 0;

static uint16_t readout_w = ACTIVE_SENSOR_WIDTH;
static uint16_t readout_h = ACTIVE_SENSOR_HEIGHT;

static bool fov_wide = false;

// SLAVE ADDR 0x78
static const uint8_t default_regs[][2] = {
    {0xfe, 0xf0},
    {0xfe, 0xf0},
    {0xfe, 0xf0},
    {0xfc, 0x06},
    {0xf6, 0x00},
    {0xf7, 0x1d},
    {0xf8, 0x85},
    {0xfa, 0x00},
    {0xf9, 0xfe},
    {0xf2, 0x00},
    /////////////////////////////////////////////////
    //////////////////ISP reg//////////////////////
    ////////////////////////////////////////////////////
    {0xfe, 0x00},
    {0x03, 0x04},
    {0x04, 0xe2},

    {0x09, 0x00},   // row start
    {0x0a, 0x00},

    {0x0b, 0x00},   // col start
    {0x0c, 0x00},

    {0x0d, 0x04},   // Window height
    {0x0e, 0xc0},

    {0x0f, 0x06},   // Window width
    {0x10, 0x52},

    {0x99, 0x11},   // Subsample
    {0x9a, 0x0E},   // Subsample mode

    {0x12, 0x2e},   //
    #if (OMV_GC2145_ROTATE == 1)
    {0x17, 0x17},   // Analog Mode 1 (vflip/mirror[1:0])
    #else
    {0x17, 0x14},   // Analog Mode 1 (vflip/mirror[1:0])
    #endif
    {0x18, 0x22},   // Analog Mode 2
    {0x19, 0x0e},
    {0x1a, 0x01},
    {0x1b, 0x4b},
    {0x1c, 0x07},
    {0x1d, 0x10},
    {0x1e, 0x88},
    {0x1f, 0x78},
    {0x20, 0x03},
    {0x21, 0x40},
    {0x22, 0xa0},
    {0x24, 0x16},
    {0x25, 0x01},
    {0x26, 0x10},
    {0x2d, 0x60},
    {0x30, 0x01},
    {0x31, 0x90},
    {0x33, 0x06},
    {0x34, 0x01},
    {0x80, 0x7f},
    {0x81, 0x26},
    {0x82, 0xfa},
    {0x83, 0x00},
    {0x84, 0x06},   //RGB565
    {0x86, 0x23},
    {0x88, 0x03},
    {0x89, 0x03},
    {0x85, 0x08},
    {0x8a, 0x00},
    {0x8b, 0x00},
    {0xb0, 0x55},
    {0xc3, 0x00},
    {0xc4, 0x80},
    {0xc5, 0x90},
    {0xc6, 0x3b},
    {0xc7, 0x46},
    {0xec, 0x06},
    {0xed, 0x04},
    {0xee, 0x60},
    {0xef, 0x90},
    {0xb6, 0x01},

    {0x90, 0x01},   // Enable crop
    {0x91, 0x00},   // Y offset
    {0x92, 0x00},
    {0x93, 0x00},   // X offset
    {0x94, 0x00},
    {0x95, 0x02},   // Window height
    {0x96, 0x58},
    {0x97, 0x03},   // Window width
    {0x98, 0x20},
    {0x99, 0x22},   // Subsample
    {0x9a, 0x0E},   // Subsample mode

    {0x9b, 0x00},
    {0x9c, 0x00},
    {0x9d, 0x00},
    {0x9e, 0x00},
    {0x9f, 0x00},
    {0xa0, 0x00},
    {0xa1, 0x00},
    {0xa2, 0x00},
    /////////////////////////////////////////
    /////////// BLK ////////////////////////
    /////////////////////////////////////////
    {0xfe, 0x00},
    {0x40, 0x42},
    {0x41, 0x00},
    {0x43, 0x5b},
    {0x5e, 0x00},
    {0x5f, 0x00},
    {0x60, 0x00},
    {0x61, 0x00},
    {0x62, 0x00},
    {0x63, 0x00},
    {0x64, 0x00},
    {0x65, 0x00},
    {0x66, 0x20},
    {0x67, 0x20},
    {0x68, 0x20},
    {0x69, 0x20},
    {0x76, 0x00},
    {0x6a, 0x08},
    {0x6b, 0x08},
    {0x6c, 0x08},
    {0x6d, 0x08},
    {0x6e, 0x08},
    {0x6f, 0x08},
    {0x70, 0x08},
    {0x71, 0x08},
    {0x76, 0x00},
    {0x72, 0xf0},
    {0x7e, 0x3c},
    {0x7f, 0x00},
    {0xfe, 0x02},
    {0x48, 0x15},
    {0x49, 0x00},
    {0x4b, 0x0b},
    {0xfe, 0x00},
    ////////////////////////////////////////
    /////////// AEC ////////////////////////
    ////////////////////////////////////////
    {0xfe, 0x01},
    {0x01, 0x04},
    {0x02, 0xc0},
    {0x03, 0x04},
    {0x04, 0x90},
    {0x05, 0x30},
    {0x06, 0x90},
    {0x07, 0x30},
    {0x08, 0x80},
    {0x09, 0x00},
    {0x0a, 0x82},
    {0x0b, 0x11},
    {0x0c, 0x10},
    {0x11, 0x10},
    {0x13, 0x68}, //7b->68 bob
    {0x17, 0x00},
    {0x1c, 0x11},
    {0x1e, 0x61},
    {0x1f, 0x35},
    {0x20, 0x40},
    {0x22, 0x40},
    {0x23, 0x20},
    {0xfe, 0x02},
    {0x0f, 0x04},
    {0xfe, 0x01},
    {0x12, 0x30}, //35
    {0x15, 0xb0},
    {0x10, 0x31},
    {0x3e, 0x28},
    {0x3f, 0xb0},
    {0x40, 0x90},
    {0x41, 0x0f},
    /////////////////////////////
    //////// INTPEE /////////////
    /////////////////////////////
    {0xfe, 0x02},
    {0x90, 0x6c},
    {0x91, 0x03},
    {0x92, 0xcb},
    {0x94, 0x33},
    {0x95, 0x84},
    {0x97, 0x65}, // 54->65 bob
    {0xa2, 0x11},
    {0xfe, 0x00},
    /////////////////////////////
    //////// DNDD///////////////
    /////////////////////////////
    {0xfe, 0x02},
    {0x80, 0xc1},
    {0x81, 0x08},
    {0x82, 0x05}, //05
    {0x83, 0x08}, //08
    {0x84, 0x0a},
    {0x86, 0xf0},
    {0x87, 0x50},
    {0x88, 0x15},
    {0x89, 0xb0},
    {0x8a, 0x30},
    {0x8b, 0x10},
    /////////////////////////////////////////
    /////////// ASDE ////////////////////////
    /////////////////////////////////////////
    {0xfe, 0x01},
    {0x21, 0x04},
    {0xfe, 0x02},
    {0xa3, 0x50},
    {0xa4, 0x20},
    {0xa5, 0x40},
    {0xa6, 0x80},
    {0xab, 0x40},
    {0xae, 0x0c},
    {0xb3, 0x46},
    {0xb4, 0x64},
    {0xb6, 0x38},
    {0xb7, 0x01}, //01
    {0xb9, 0x2b}, //2b
    {0x3c, 0x04}, //04
    {0x3d, 0x15}, //15
    {0x4b, 0x06}, //06
    {0x4c, 0x20},
    {0xfe, 0x00},
    /////////////////////////////////////////
    /////////// GAMMA   ////////////////////////
    /////////////////////////////////////////
    ///////////////////gamma1////////////////////
    {0xfe, 0x02},
    {0x10, 0x09},
    {0x11, 0x0d},
    {0x12, 0x13},
    {0x13, 0x19},
    {0x14, 0x27},
    {0x15, 0x37},
    {0x16, 0x45},
    {0x17, 0x53},
    {0x18, 0x69},
    {0x19, 0x7d},
    {0x1a, 0x8f},
    {0x1b, 0x9d},
    {0x1c, 0xa9},
    {0x1d, 0xbd},
    {0x1e, 0xcd},
    {0x1f, 0xd9},
    {0x20, 0xe3},
    {0x21, 0xea},
    {0x22, 0xef},
    {0x23, 0xf5},
    {0x24, 0xf9},
    {0x25, 0xff},
    {0xfe, 0x00},
    {0xc6, 0x20},
    {0xc7, 0x2b},
    ///////////////////gamma2////////////////////
    {0xfe, 0x02},
    {0x26, 0x0f},
    {0x27, 0x14},
    {0x28, 0x19},
    {0x29, 0x1e},
    {0x2a, 0x27},
    {0x2b, 0x33},
    {0x2c, 0x3b},
    {0x2d, 0x45},
    {0x2e, 0x59},
    {0x2f, 0x69},
    {0x30, 0x7c},
    {0x31, 0x89},
    {0x32, 0x98},
    {0x33, 0xae},
    {0x34, 0xc0},
    {0x35, 0xcf},
    {0x36, 0xda},
    {0x37, 0xe2},
    {0x38, 0xe9},
    {0x39, 0xf3},
    {0x3a, 0xf9},
    {0x3b, 0xff},
    ///////////////////////////////////////////////
    ///////////YCP ///////////////////////
    ///////////////////////////////////////////////
    {0xfe, 0x02},
    {0xd1, 0x32}, //32->2d
    {0xd2, 0x32}, //32->2d bob
    {0xd3, 0x40},
    {0xd6, 0xf0},
    {0xd7, 0x10},
    {0xd8, 0xda},
    {0xdd, 0x14},
    {0xde, 0x86},
    {0xed, 0x80}, //80
    {0xee, 0x00}, //00
    {0xef, 0x3f},
    {0xd8, 0xd8},
    ///////////////////abs/////////////////
    {0xfe, 0x01},
    {0x9f, 0x40},
    /////////////////////////////////////////////
    //////////////////////// LSC ///////////////
    //////////////////////////////////////////
    {0xfe, 0x01},
    {0xc2, 0x14},
    {0xc3, 0x0d},
    {0xc4, 0x0c},
    {0xc8, 0x15},
    {0xc9, 0x0d},
    {0xca, 0x0a},
    {0xbc, 0x24},
    {0xbd, 0x10},
    {0xbe, 0x0b},
    {0xb6, 0x25},
    {0xb7, 0x16},
    {0xb8, 0x15},
    {0xc5, 0x00},
    {0xc6, 0x00},
    {0xc7, 0x00},
    {0xcb, 0x00},
    {0xcc, 0x00},
    {0xcd, 0x00},
    {0xbf, 0x07},
    {0xc0, 0x00},
    {0xc1, 0x00},
    {0xb9, 0x00},
    {0xba, 0x00},
    {0xbb, 0x00},
    {0xaa, 0x01},
    {0xab, 0x01},
    {0xac, 0x00},
    {0xad, 0x05},
    {0xae, 0x06},
    {0xaf, 0x0e},
    {0xb0, 0x0b},
    {0xb1, 0x07},
    {0xb2, 0x06},
    {0xb3, 0x17},
    {0xb4, 0x0e},
    {0xb5, 0x0e},
    {0xd0, 0x09},
    {0xd1, 0x00},
    {0xd2, 0x00},
    {0xd6, 0x08},
    {0xd7, 0x00},
    {0xd8, 0x00},
    {0xd9, 0x00},
    {0xda, 0x00},
    {0xdb, 0x00},
    {0xd3, 0x0a},
    {0xd4, 0x00},
    {0xd5, 0x00},
    {0xa4, 0x00},
    {0xa5, 0x00},
    {0xa6, 0x77},
    {0xa7, 0x77},
    {0xa8, 0x77},
    {0xa9, 0x77},
    {0xa1, 0x80},
    {0xa2, 0x80},

    {0xfe, 0x01},
    {0xdf, 0x0d},
    {0xdc, 0x25},
    {0xdd, 0x30},
    {0xe0, 0x77},
    {0xe1, 0x80},
    {0xe2, 0x77},
    {0xe3, 0x90},
    {0xe6, 0x90},
    {0xe7, 0xa0},
    {0xe8, 0x90},
    {0xe9, 0xa0},
    {0xfe, 0x00},
    ///////////////////////////////////////////////
    /////////// AWB////////////////////////
    ///////////////////////////////////////////////
    {0xfe, 0x01},
    {0x4f, 0x00},
    {0x4f, 0x00},
    {0x4b, 0x01},
    {0x4f, 0x00},

    {0x4c, 0x01}, // D75
    {0x4d, 0x71},
    {0x4e, 0x01},
    {0x4c, 0x01},
    {0x4d, 0x91},
    {0x4e, 0x01},
    {0x4c, 0x01},
    {0x4d, 0x70},
    {0x4e, 0x01},
    {0x4c, 0x01}, // D65
    {0x4d, 0x90},
    {0x4e, 0x02},
    {0x4c, 0x01},
    {0x4d, 0xb0},
    {0x4e, 0x02},
    {0x4c, 0x01},
    {0x4d, 0x8f},
    {0x4e, 0x02},
    {0x4c, 0x01},
    {0x4d, 0x6f},
    {0x4e, 0x02},
    {0x4c, 0x01},
    {0x4d, 0xaf},
    {0x4e, 0x02},
    {0x4c, 0x01},
    {0x4d, 0xd0},
    {0x4e, 0x02},
    {0x4c, 0x01},
    {0x4d, 0xf0},
    {0x4e, 0x02},
    {0x4c, 0x01},
    {0x4d, 0xcf},
    {0x4e, 0x02},
    {0x4c, 0x01},
    {0x4d, 0xef},
    {0x4e, 0x02},
    {0x4c, 0x01}, //D50
    {0x4d, 0x6e},
    {0x4e, 0x03},
    {0x4c, 0x01},
    {0x4d, 0x8e},
    {0x4e, 0x03},
    {0x4c, 0x01},
    {0x4d, 0xae},
    {0x4e, 0x03},
    {0x4c, 0x01},
    {0x4d, 0xce},
    {0x4e, 0x03},
    {0x4c, 0x01},
    {0x4d, 0x4d},
    {0x4e, 0x03},
    {0x4c, 0x01},
    {0x4d, 0x6d},
    {0x4e, 0x03},
    {0x4c, 0x01},
    {0x4d, 0x8d},
    {0x4e, 0x03},
    {0x4c, 0x01},
    {0x4d, 0xad},
    {0x4e, 0x03},
    {0x4c, 0x01},
    {0x4d, 0xcd},
    {0x4e, 0x03},
    {0x4c, 0x01},
    {0x4d, 0x4c},
    {0x4e, 0x03},
    {0x4c, 0x01},
    {0x4d, 0x6c},
    {0x4e, 0x03},
    {0x4c, 0x01},
    {0x4d, 0x8c},
    {0x4e, 0x03},
    {0x4c, 0x01},
    {0x4d, 0xac},
    {0x4e, 0x03},
    {0x4c, 0x01},
    {0x4d, 0xcc},
    {0x4e, 0x03},
    {0x4c, 0x01},
    {0x4d, 0xcb},
    {0x4e, 0x03},
    {0x4c, 0x01},
    {0x4d, 0x4b},
    {0x4e, 0x03},
    {0x4c, 0x01},
    {0x4d, 0x6b},
    {0x4e, 0x03},
    {0x4c, 0x01},
    {0x4d, 0x8b},
    {0x4e, 0x03},
    {0x4c, 0x01},
    {0x4d, 0xab},
    {0x4e, 0x03},
    {0x4c, 0x01}, //CWF
    {0x4d, 0x8a},
    {0x4e, 0x04},
    {0x4c, 0x01},
    {0x4d, 0xaa},
    {0x4e, 0x04},
    {0x4c, 0x01},
    {0x4d, 0xca},
    {0x4e, 0x04},
    {0x4c, 0x01},
    {0x4d, 0xca},
    {0x4e, 0x04},
    {0x4c, 0x01},
    {0x4d, 0xc9},
    {0x4e, 0x04},
    {0x4c, 0x01},
    {0x4d, 0x8a},
    {0x4e, 0x04},
    {0x4c, 0x01},
    {0x4d, 0x89},
    {0x4e, 0x04},
    {0x4c, 0x01},
    {0x4d, 0xa9},
    {0x4e, 0x04},
    {0x4c, 0x02}, //tl84
    {0x4d, 0x0b},
    {0x4e, 0x05},
    {0x4c, 0x02},
    {0x4d, 0x0a},
    {0x4e, 0x05},
    {0x4c, 0x01},
    {0x4d, 0xeb},
    {0x4e, 0x05},
    {0x4c, 0x01},
    {0x4d, 0xea},
    {0x4e, 0x05},
    {0x4c, 0x02},
    {0x4d, 0x09},
    {0x4e, 0x05},
    {0x4c, 0x02},
    {0x4d, 0x29},
    {0x4e, 0x05},
    {0x4c, 0x02},
    {0x4d, 0x2a},
    {0x4e, 0x05},
    {0x4c, 0x02},
    {0x4d, 0x4a},
    {0x4e, 0x05},
    {0x4c, 0x02},
    {0x4d, 0x8a},
    {0x4e, 0x06},
    {0x4c, 0x02},
    {0x4d, 0x49},
    {0x4e, 0x06},
    {0x4c, 0x02},
    {0x4d, 0x69},
    {0x4e, 0x06},
    {0x4c, 0x02},
    {0x4d, 0x89},
    {0x4e, 0x06},
    {0x4c, 0x02},
    {0x4d, 0xa9},
    {0x4e, 0x06},
    {0x4c, 0x02},
    {0x4d, 0x48},
    {0x4e, 0x06},
    {0x4c, 0x02},
    {0x4d, 0x68},
    {0x4e, 0x06},
    {0x4c, 0x02},
    {0x4d, 0x69},
    {0x4e, 0x06},
    {0x4c, 0x02}, //H
    {0x4d, 0xca},
    {0x4e, 0x07},
    {0x4c, 0x02},
    {0x4d, 0xc9},
    {0x4e, 0x07},
    {0x4c, 0x02},
    {0x4d, 0xe9},
    {0x4e, 0x07},
    {0x4c, 0x03},
    {0x4d, 0x09},
    {0x4e, 0x07},
    {0x4c, 0x02},
    {0x4d, 0xc8},
    {0x4e, 0x07},
    {0x4c, 0x02},
    {0x4d, 0xe8},
    {0x4e, 0x07},
    {0x4c, 0x02},
    {0x4d, 0xa7},
    {0x4e, 0x07},
    {0x4c, 0x02},
    {0x4d, 0xc7},
    {0x4e, 0x07},
    {0x4c, 0x02},
    {0x4d, 0xe7},
    {0x4e, 0x07},
    {0x4c, 0x03},
    {0x4d, 0x07},
    {0x4e, 0x07},

    {0x4f, 0x01},
    {0x50, 0x80},
    {0x51, 0xa8},
    {0x52, 0x47},
    {0x53, 0x38},
    {0x54, 0xc7},
    {0x56, 0x0e},
    {0x58, 0x08},
    {0x5b, 0x00},
    {0x5c, 0x74},
    {0x5d, 0x8b},
    {0x61, 0xdb},
    {0x62, 0xb8},
    {0x63, 0x86},
    {0x64, 0xc0},
    {0x65, 0x04},
    {0x67, 0xa8},
    {0x68, 0xb0},
    {0x69, 0x00},
    {0x6a, 0xa8},
    {0x6b, 0xb0},
    {0x6c, 0xaf},
    {0x6d, 0x8b},
    {0x6e, 0x50},
    {0x6f, 0x18},
    {0x73, 0xf0},
    {0x70, 0x0d},
    {0x71, 0x60},
    {0x72, 0x80},
    {0x74, 0x01},
    {0x75, 0x01},
    {0x7f, 0x0c},
    {0x76, 0x70},
    {0x77, 0x58},
    {0x78, 0xa0},
    {0x79, 0x5e},
    {0x7a, 0x54},
    {0x7b, 0x58},
    {0xfe, 0x00},
    //////////////////////////////////////////
    ///////////CC////////////////////////
    //////////////////////////////////////////
    {0xfe, 0x02},
    {0xc0, 0x01},
    {0xc1, 0x44},
    {0xc2, 0xfd},
    {0xc3, 0x04},
    {0xc4, 0xF0},
    {0xc5, 0x48},
    {0xc6, 0xfd},
    {0xc7, 0x46},
    {0xc8, 0xfd},
    {0xc9, 0x02},
    {0xca, 0xe0},
    {0xcb, 0x45},
    {0xcc, 0xec},
    {0xcd, 0x48},
    {0xce, 0xf0},
    {0xcf, 0xf0},
    {0xe3, 0x0c},
    {0xe4, 0x4b},
    {0xe5, 0xe0},
    //////////////////////////////////////////
    ///////////ABS ////////////////////
    //////////////////////////////////////////
    {0xfe, 0x01},
    {0x9f, 0x40},
    {0xfe, 0x00},

    //////////////////////////////////////
    ///////////  OUTPUT   ////////////////
    //////////////////////////////////////
    {0xfe, 0x00},
    {0xf2, 0x0f},

    ///////////////dark sun////////////////////
    {0xfe, 0x02},
    {0x40, 0xbf},
    {0x46, 0xcf},
    {0xfe, 0x00},

    //////////////frame rate control/////////
    {0xfe, 0x00},
    {0x05, 0x01},   // HBLANK
    {0x06, 0x1C},
    {0x07, 0x00},   // VBLANK
    {0x08, 0x32},
    {0x11, 0x00},   // SH Delay
    {0x12, 0x1D},
    {0x13, 0x00},   // St
    {0x14, 0x00},   // Et

    {0xfe, 0x01},
    {0x3c, 0x00},
    {0x3d, 0x04},
    {0xfe, 0x00},

    {0x00, 0x00},
};

static int reset(omv_csi_t *csi) {
    int ret = 0;

    readout_x = 0;
    readout_y = 0;

    readout_w = ACTIVE_SENSOR_WIDTH;
    readout_h = ACTIVE_SENSOR_HEIGHT;

    fov_wide = false;

    // Write default registers
    for (int i = 0; default_regs[i][0]; i++) {
        ret |= omv_i2c_writeb(csi->i2c, csi->slv_addr, default_regs[i][0], default_regs[i][1]);
    }

    // Delay 10 ms
    mp_hal_delay_ms(10);

    return ret;
}

static int sleep(omv_csi_t *csi, int enable) {
    int ret = 0;

    if (enable) {
        ret |= omv_i2c_writeb(csi->i2c, csi->slv_addr, 0xF2, 0x0);
        ret |= omv_i2c_writeb(csi->i2c, csi->slv_addr, 0xF7, 0x10);
        ret |= omv_i2c_writeb(csi->i2c, csi->slv_addr, 0xFC, 0x01);
    } else {
        ret |= omv_i2c_writeb(csi->i2c, csi->slv_addr, 0xF2, 0x0F);
        ret |= omv_i2c_writeb(csi->i2c, csi->slv_addr, 0xF7, 0x1d);
        ret |= omv_i2c_writeb(csi->i2c, csi->slv_addr, 0xFC, 0x06);
    }

    return ret;
}

static int read_reg(omv_csi_t *csi, uint16_t reg_addr) {
    uint8_t reg_data;
    if (omv_i2c_readb(csi->i2c, csi->slv_addr, reg_addr, &reg_data) != 0) {
        return -1;
    }
    return reg_data;
}

static int write_reg(omv_csi_t *csi, uint16_t reg_addr, uint16_t reg_data) {
    return omv_i2c_writeb(csi->i2c, csi->slv_addr, reg_addr, reg_data);
}

static int set_pixformat(omv_csi_t *csi, pixformat_t pixformat) {
    int ret = 0;
    uint8_t reg;

    // P0 regs
    ret |= omv_i2c_writeb(csi->i2c, csi->slv_addr, 0xFE, 0x00);

    // Read current output format reg
    ret |= omv_i2c_readb(csi->i2c, csi->slv_addr, REG_OUTPUT_FMT, &reg);

    switch (pixformat) {
        case PIXFORMAT_RGB565:
            ret |= omv_i2c_writeb(csi->i2c, csi->slv_addr,
                                  REG_OUTPUT_FMT, REG_OUTPUT_SET_FMT(reg, REG_OUTPUT_FMT_RGB565));
            break;
        case PIXFORMAT_YUV422:
        case PIXFORMAT_GRAYSCALE:
            ret |= omv_i2c_writeb(csi->i2c, csi->slv_addr,
                                  REG_OUTPUT_FMT, REG_OUTPUT_SET_FMT(reg, REG_OUTPUT_FMT_YCBYCR));
            break;
        case PIXFORMAT_BAYER:
            ret |= omv_i2c_writeb(csi->i2c, csi->slv_addr,
                                  REG_OUTPUT_FMT, REG_OUTPUT_SET_FMT(reg, REG_OUTPUT_FMT_BAYER));
            break;
        default:
            return -1;
    }

    return ret;
}

static int set_window(omv_csi_t *csi, uint16_t reg, uint16_t x, uint16_t y, uint16_t w, uint16_t h) {
    int ret = 0;

    // P0 regs
    ret |= omv_i2c_writeb(csi->i2c, csi->slv_addr, 0xFE, 0x00);

    // Y/row offset
    ret |= omv_i2c_writeb(csi->i2c, csi->slv_addr, reg++, y >> 8);
    ret |= omv_i2c_writeb(csi->i2c, csi->slv_addr, reg++, y & 0xff);

    // X/col offset
    ret |= omv_i2c_writeb(csi->i2c, csi->slv_addr, reg++, x >> 8);
    ret |= omv_i2c_writeb(csi->i2c, csi->slv_addr, reg++, x & 0xff);

    // Window height
    ret |= omv_i2c_writeb(csi->i2c, csi->slv_addr, reg++, h >> 8);
    ret |= omv_i2c_writeb(csi->i2c, csi->slv_addr, reg++, h & 0xff);

    // Window width
    ret |= omv_i2c_writeb(csi->i2c, csi->slv_addr, reg++, w >> 8);
    ret |= omv_i2c_writeb(csi->i2c, csi->slv_addr, reg++, w & 0xff);

    return ret;
}

static int set_framesize(omv_csi_t *csi, omv_csi_framesize_t framesize) {
    int ret = 0;

    uint16_t w = resolution[framesize][0];
    uint16_t h = resolution[framesize][1];

    // Invalid resolution.
    if ((w > ACTIVE_SENSOR_WIDTH) || (h > ACTIVE_SENSOR_HEIGHT)) {
        return -1;
    }

    // Step 0: Clamp readout settings.

    readout_w = IM_MAX(readout_w, w);
    readout_h = IM_MAX(readout_h, h);

    int readout_x_max = (ACTIVE_SENSOR_WIDTH - readout_w) / 2;
    int readout_y_max = (ACTIVE_SENSOR_HEIGHT - readout_h) / 2;
    readout_x = IM_CLAMP(readout_x, -readout_x_max, readout_x_max);
    readout_y = IM_CLAMP(readout_y, -readout_y_max, readout_y_max);

    // Step 1: Determine sub-readout window.

    uint16_t ratio = fast_floorf(IM_MIN(readout_w / ((float) w), readout_h / ((float) h)));

    // Limit the maximum amount of scaling allowed to keep the frame rate up.
    ratio = IM_MIN(ratio, (fov_wide ? 5 : 3));

    if (!(ratio % 2)) {
        // camera outputs messed up bayer images at even ratios for some reason...
        ratio -= 1;
    }

    uint16_t sub_readout_w = w * ratio;
    uint16_t sub_readout_h = h * ratio;

    // Step 2: Determine horizontal and vertical start and end points.

    uint16_t sensor_w = sub_readout_w + DUMMY_WIDTH_BUFFER; // camera hardware needs dummy pixels to sync
    uint16_t sensor_h = sub_readout_h + DUMMY_HEIGHT_BUFFER; // camera hardware needs dummy lines to sync

    uint16_t sensor_x = IM_CLAMP((((ACTIVE_SENSOR_WIDTH - sensor_w) / 4) - (readout_x / 2)) * 2,
                                 -(DUMMY_WIDTH_BUFFER / 2), ACTIVE_SENSOR_WIDTH - sensor_w) + DUMMY_COLUMNS; // must be multiple of 2

    uint16_t sensor_y = IM_CLAMP((((ACTIVE_SENSOR_HEIGHT - sensor_h) / 4) - (readout_y / 2)) * 2,
                                 -(DUMMY_HEIGHT_BUFFER / 2), ACTIVE_SENSOR_HEIGHT - sensor_h) + DUMMY_LINES; // must be multiple of 2

    // Step 3: Write regs.

    // Set Readout window first.
    ret |= set_window(csi, 0x09, sensor_x, sensor_y, sensor_w, sensor_h);

    // Set cropping window next.
    ret |= set_window(csi, 0x91, 0, 0, w, h);

    // Enable crop
    ret |= omv_i2c_writeb(csi->i2c, csi->slv_addr, 0x90, 0x01);

    // Set Sub-sampling ratio and mode
    ret |= omv_i2c_writeb(csi->i2c, csi->slv_addr, 0x99, ((ratio << 4) | (ratio)));
    ret |= omv_i2c_writeb(csi->i2c, csi->slv_addr, 0x9A, 0x0E);

    return ret;
}

static int set_hmirror(omv_csi_t *csi, int enable) {
    int ret = 0;
    uint8_t reg;
    #if (OMV_GC2145_ROTATE == 1)
    enable = !enable;
    #endif

    // P0 regs
    ret |= omv_i2c_writeb(csi->i2c, csi->slv_addr, 0xFE, 0x00);
    ret |= omv_i2c_readb(csi->i2c, csi->slv_addr, REG_AMODE1, &reg);
    ret |= omv_i2c_writeb(csi->i2c, csi->slv_addr, REG_AMODE1, REG_AMODE1_SET_HMIRROR(reg, enable));
    return ret;
}

static int set_vflip(omv_csi_t *csi, int enable) {
    int ret = 0;
    uint8_t reg;
    #if (OMV_GC2145_ROTATE == 1)
    enable = !enable;
    #endif

    // P0 regs
    ret |= omv_i2c_writeb(csi->i2c, csi->slv_addr, 0xFE, 0x00);
    ret |= omv_i2c_readb(csi->i2c, csi->slv_addr, REG_AMODE1, &reg);
    ret |= omv_i2c_writeb(csi->i2c, csi->slv_addr, REG_AMODE1, REG_AMODE1_SET_VMIRROR(reg, enable));
    return ret;
}

static int set_auto_exposure(omv_csi_t *csi, int enable, int exposure_us) {
    int ret = 0;
    uint8_t reg;
    ret |= omv_i2c_writeb(csi->i2c, csi->slv_addr, 0xFE, 0x00);
    ret |= omv_i2c_readb(csi->i2c, csi->slv_addr, 0xb6, &reg);
    ret |= omv_i2c_writeb(csi->i2c, csi->slv_addr, 0xb6, (reg & 0xFE) | (enable & 0x01));
    return ret;
}

static int set_auto_whitebal(omv_csi_t *csi, int enable, float r_gain_db, float g_gain_db, float b_gain_db) {
    int ret = 0;
    uint8_t reg;
    ret |= omv_i2c_writeb(csi->i2c, csi->slv_addr, 0xFE, 0x00);
    ret |= omv_i2c_readb(csi->i2c, csi->slv_addr, 0x82, &reg);
    ret |= omv_i2c_writeb(csi->i2c, csi->slv_addr, 0x82, (reg & 0xFD) | ((enable & 0x01) << 1));
    return ret;
}

static int ioctl(omv_csi_t *csi, int request, va_list ap) {
    int ret = 0;

    switch (request) {
        case OMV_CSI_IOCTL_SET_READOUT_WINDOW: {
            int tmp_readout_x = va_arg(ap, int);
            int tmp_readout_y = va_arg(ap, int);
            int tmp_readout_w = IM_CLAMP(va_arg(ap, int), resolution[csi->framesize][0], ACTIVE_SENSOR_WIDTH);
            int tmp_readout_h = IM_CLAMP(va_arg(ap, int), resolution[csi->framesize][1], ACTIVE_SENSOR_HEIGHT);
            int readout_x_max = (ACTIVE_SENSOR_WIDTH - tmp_readout_w) / 2;
            int readout_y_max = (ACTIVE_SENSOR_HEIGHT - tmp_readout_h) / 2;
            tmp_readout_x = IM_CLAMP(tmp_readout_x, -readout_x_max, readout_x_max);
            tmp_readout_y = IM_CLAMP(tmp_readout_y, -readout_y_max, readout_y_max);
            bool changed = (tmp_readout_x != readout_x) || (tmp_readout_y != readout_y) ||
                           (tmp_readout_w != readout_w) || (tmp_readout_h != readout_h);
            readout_x = tmp_readout_x;
            readout_y = tmp_readout_y;
            readout_w = tmp_readout_w;
            readout_h = tmp_readout_h;
            if (changed && (csi->framesize != OMV_CSI_FRAMESIZE_INVALID)) {
                set_framesize(csi, csi->framesize);
            }
            break;
        }
        case OMV_CSI_IOCTL_GET_READOUT_WINDOW: {
            *va_arg(ap, int *) = readout_x;
            *va_arg(ap, int *) = readout_y;
            *va_arg(ap, int *) = readout_w;
            *va_arg(ap, int *) = readout_h;
            break;
        }
        case OMV_CSI_IOCTL_SET_FOV_WIDE: {
            fov_wide = va_arg(ap, int);
            break;
        }
        case OMV_CSI_IOCTL_GET_FOV_WIDE: {
            *va_arg(ap, int *) = fov_wide;
            break;
        }
        default: {
            ret = -1;
            break;
        }
    }

    return ret;
}

int gc2145_init(omv_csi_t *csi) {
    // Initialize csi structure.
    csi->reset = reset;
    csi->sleep = sleep;
    csi->read_reg = read_reg;
    csi->write_reg = write_reg;
    csi->set_pixformat = set_pixformat;
    csi->set_framesize = set_framesize;
    csi->set_hmirror = set_hmirror;
    csi->set_vflip = set_vflip;
    csi->set_auto_exposure = set_auto_exposure;
    csi->set_auto_whitebal = set_auto_whitebal;
    csi->ioctl = ioctl;

    // Set csi flags
    csi->vsync_pol = 0;
    csi->hsync_pol = 0;
    csi->pixck_pol = 1;
    csi->frame_sync = 0;
    csi->mono_bpp = 2;
    csi->rgb_swap = 1;
    csi->cfa_format = SUBFORMAT_ID_GBRG;

    return 0;
}
#endif // (OMV_GC2145_ENABLE == 1)
