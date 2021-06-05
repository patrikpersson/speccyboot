;;
;; Module context_switch:
;;
;; Protecting SpeccyBoot runtime data during snapshot loading, and switching to
;; the final Spectrum system state from header data.
;;
;; Part of SpeccyBoot <https://github.com/patrikpersson/speccyboot>
;;
;; ----------------------------------------------------------------------------
;;
;; Copyright (c) 2009-  Patrik Persson
;;
;; Permission is hereby granted, free of charge, to any person
;; obtaining a copy of this software and associated documentation
;; files (the "Software"), to deal in the Software without
;; restriction, including without limitation the rights to use,
;; copy, modify, merge, publish, distribute, sublicense, and/or sell
;; copies of the Software, and to permit persons to whom the
;; Software is furnished to do so, subject to the following
;; conditions:
;;
;; The above copyright notice and this permission notice shall be
;; included in all copies or substantial portions of the Software.
;;
;; THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
;; EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
;; OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
;; NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
;; HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
;; WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
;; FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
;; OTHER DEALINGS IN THE SOFTWARE.
;;

;; ----------------------------------------------------------------------------
;; Evacuation concerns protecting the RAM used by SpeccyBoot (runtime data)
;; while a snapshot is being loaded into RAM. It is performed in three steps:
;;
;; 1. When loading the snapshot's RAM contents for runtime data area, the
;;    loaded data is instead stored in a temporary evacuation buffer (within
;;    Spectrum RAM).
;;
;; 2. When the last byte of the loaded data above has been loaded, the entire
;;    temporary evacuation buffer is written to ENC28J60 on-chip SRAM.
;;
;; 3. When the entire .z80 snapshot has been loaded, the runtime data is
;;    overwritten with the corresponding data from ENC28J60 on-chip SRAM.
;;
;; Memory layout for evacuation:
;;
;; address range      size  contents
;; -------------      ----  --------
;; 0x0000 .. 0x07FF   2kB   SpeccyBoot ROM
;; 0x4000 .. 0x57FF   6kB   Video RAM (bitmap)
;; 0x5800 .. 0x5AFF   768B  Video RAM (attributes, progress display)     (!)
;; 0x5B00 .. 0x5FFF  1280B  stack, data, resident code (.z80 loader)     (!)
;; 0x6000 ..                non-resident code (menu)
;;
;; The area 0x5800 - 0x5FFF, marked with (!) above, needs to be preserved
;; during snapshot loading. When bytes destined for these addresses are
;; received, they are instead stored in the ENC28J60's on-chip SRAM:
;;
;; 0x1800 .. 0x1FFF   3kB   data destined for addresses 0x5800 .. 0x5FFF in
;;                          the Spectrum RAM (temporary storage during loading)
;; ----------------------------------------------------------------------------

;; ----------------------------------------------------------------------------
;; VRAM trampoline layout. Split onto multiple pixel lines, to reduce the
;; number of distorted character cells to 5.
;;
;; 0x4000:
;;    out (0x9f), a
;;    jp  0x4100
;; 0x4100:
;;    ld  a, #N      (value to be set for I below)
;;    jp  0x4200
;; 0x4200:
;;    ld  i, a
;;    jp  0x4300
;; 0x4300:
;;    ld  a, #N
;;    jp  0x4400
;; 0x4400:
;;    im0/im1/im2   (depending on snapshot interrupt mode)
;;    jp  0x4500
;; 0x4500:
;;    nop           (for symmetry of JPs)
;;    ei / nop      (depending on whether interrupts are to be enabled)
;;    jp  NN
;;
;; (state for registers BC, DE, HL, SP, F, R follow
;; in the remaining scan lines of this 5-cell trampoline)
;; ----------------------------------------------------------------------------

VRAM_TRAMPOLINE_START       = 0x4000
VRAM_TRAMPOLINE_OUT         = VRAM_TRAMPOLINE_START
VRAM_TRAMPOLINE_LD_A_FOR_I  = 0x4100
VRAM_TRAMPOLINE_LD_I        = 0x4200
VRAM_TRAMPOLINE_LD_A        = 0x4300
VRAM_TRAMPOLINE_IM          = 0x4400
VRAM_TRAMPOLINE_NOP         = 0x4500
VRAM_TRAMPOLINE_EI_OR_NOP   = 0x4501
VRAM_TRAMPOLINE_JP_FINAL    = 0x4502

;; ----------------------------------------------------------------------------
;; Register state, stored in VRAM along with the trampoline
;; ----------------------------------------------------------------------------

VRAM_REGSTATE_PC            = VRAM_TRAMPOLINE_JP_FINAL + 1

VRAM_REGSTATE_I             = VRAM_TRAMPOLINE_LD_A_FOR_I + 1

VRAM_REGSTATE_A             = VRAM_TRAMPOLINE_LD_A + 1

VRAM_REGSTATE_BC_HL_F       = 0x4600
VRAM_REGSTATE_F             = 0x4604

VRAM_REGSTATE_SP            = 0x4700
VRAM_REGSTATE_R             = 0x4702

;; ----------------------------------------------------------------------------
;; Runtime data (the stuff to evacuate). Note that the z80_loader code
;; requires RUNTIME_DATA_LENGTH to be a multiple of 0x400 (for kilobyte
;; counter display)
;; ----------------------------------------------------------------------------

RUNTIME_DATA                = 0x5800
RUNTIME_DATA_LENGTH         = 0x0800

;; ----------------------------------------------------------------------------
;; Buffer to write evacuated data into, before we write all off it to the
;; ENC28J60.
;; ----------------------------------------------------------------------------

EVACUATION_TEMP_BUFFER      = 0x7000

;; ----------------------------------------------------------------------------
;; Masks for meaning of snapshot_flags
;; ----------------------------------------------------------------------------

SNAPSHOT_FLAGS_COMPRESSED_MASK = 0x20

;; ----------------------------------------------------------------------------
;; .z80 snapshot file header
;;
;; https://worldofspectrum.org/faq/reference/z80format.htm
;; ----------------------------------------------------------------------------

Z80_HEADER_RESIDENT_SIZE         = 55  ;; no of bytes to keep in RAM

Z80_HEADER_OFFSET_A              = 0
Z80_HEADER_OFFSET_F              = 1
Z80_HEADER_OFFSET_BC_HL          = 2
Z80_HEADER_OFFSET_PC             = 6
Z80_HEADER_OFFSET_SP             = 8
Z80_HEADER_OFFSET_I              = 10
Z80_HEADER_OFFSET_R              = 11
Z80_HEADER_OFFSET_MISC_FLAGS     = 12
Z80_HEADER_OFFSET_DE             = 13
Z80_HEADER_OFFSET_BC_P           = 15
Z80_HEADER_OFFSET_DE_P           = 17
Z80_HEADER_OFFSET_HL_P           = 19
Z80_HEADER_OFFSET_A_P            = 21
Z80_HEADER_OFFSET_F_P            = 22
Z80_HEADER_OFFSET_IY             = 23
Z80_HEADER_OFFSET_IX             = 25
Z80_HEADER_OFFSET_IFF1           = 27
Z80_HEADER_OFFSET_INT_MODE       = 29

Z80_HEADER_OFFSET_EXT_LENGTH     = 30
Z80_HEADER_OFFSET_EXT_PC         = 32
Z80_HEADER_OFFSET_HW_TYPE        = 34
Z80_HEADER_OFFSET_HW_STATE_7FFD  = 35
Z80_HEADER_OFFSET_HW_STATE_FFFD  = 38

Z80_HEADER_OFFSET_HW_STATE_SND   = 39

;; ----------------------------------------------------------------------------
;; Value for Z80_HEADER_OFFSET_HW_TYPE, indicating a 128k machine.
;; Any value >= 3 is considered here to mean 128k, although some versions of
;; the .z80 format use the value 3 to denote a 48k machine with M.G.T.:
;; https://worldofspectrum.org/faq/reference/z80format.htm
;; ----------------------------------------------------------------------------

SNAPSHOT_128K                    = 3

;; ----------------------------------------------------------------------------
;; Stored .z80 snapshot header
;; ----------------------------------------------------------------------------

    .globl stored_snapshot_header

;; ----------------------------------------------------------------------------
;; Prepare context data + VRAM trampoline
;; ----------------------------------------------------------------------------
    .globl prepare_context

;; ----------------------------------------------------------------------------
;; Restore application data from ENC28J60 SRAM, restore register values
;; and system state from the stored .z80 header, and execute the application.
;; ----------------------------------------------------------------------------
    .globl context_switch
