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
;;    ld  a, #N
;;    jp  0x4200
;; 0x4200:
;;    nop           (for symmetry of JPs)
;;    ei / nop      (depending on whether interrupts are to be enabled)
;;    jp  NN
;;
;; (state for registers BC, HL, F, SP, R follow
;; in the remaining scan lines of this 5-cell trampoline)
;; ----------------------------------------------------------------------------

VRAM_TRAMPOLINE_START       = 0x4000
VRAM_TRAMPOLINE_LD_A        = 0x4100
VRAM_TRAMPOLINE_EI_OR_NOP   = 0x4200
VRAM_TRAMPOLINE_JP_FINAL    = 0x4201

;; ----------------------------------------------------------------------------
;; Register state, stored in VRAM along with the trampoline
;; ----------------------------------------------------------------------------

VRAM_REGSTATE_PC            = VRAM_TRAMPOLINE_JP_FINAL + 1

VRAM_REGSTATE_A             = VRAM_TRAMPOLINE_LD_A + 1

VRAM_REGSTATE_BC_HL_F       = 0x4300
VRAM_REGSTATE_F             = 0x4304

VRAM_REGSTATE_SP            = 0x4400
VRAM_REGSTATE_R             = 0x4402

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

EVACUATION_TEMP_BUFFER      = 0x6000

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
;; Offset to the R register when stored, to compensate for the fact that R
;; is affected by the execution of the trampoline.
;;
;; Calibrate this offset as follows:
;;
;; - Set it temporarily to 0, and run one of the test images
;; - Assume E = expected value of R        (0x2e in test image),
;;          N = actual value of R (as presented in binary by the test image)
;; - Set REG_R_ADJUSTMENT := (E - N)
;; ----------------------------------------------------------------------------

REG_R_ADJUSTMENT                 = 0xF8

;; ----------------------------------------------------------------------------
;; Restore application data from ENC28J60 SRAM, restore register values
;; and system state from the stored .z80 header, and execute the application.
;; ----------------------------------------------------------------------------
    .globl context_switch

;; ----------------------------------------------------------------------------
;; Code copied to VRAM for trampoline
;; ----------------------------------------------------------------------------
    .globl trampoline_data

;; ----------------------------------------------------------------------------
;; 128k memory configuration + 48k/128k flag  (2 bytes _DATA)
;; ----------------------------------------------------------------------------
    .globl memory_state

;; ============================================================================
;; Macro: prepare context switch
;;
;; Write trampoline to (what will be) VRAM,
;; store register values,
;; copy runtime data to ENC28J60 SRAM
;; ============================================================================

    .macro  prepare_context_switch

    ;; ========================================================================
    ;; Prepare VRAM trampoline.
    ;;
    ;; Clear out the top-left five character cells, by setting ink colour
    ;; to the same as the paper colour.
    ;;
    ;; (These character cells are used as temporary storage for the trampoline
    ;; below.)
    ;; ========================================================================

    ld   hl, #EVACUATION_TEMP_BUFFER        ;; points to attribute data
    ld   b, #5
clear_cells_loop:
    ld   a, (hl)
    rra
    rra
    rra
    and  a, #7
    ld   c, a
    ld   a, (hl)
    and  a, #0xf8
    or   a, c
    ld   (hl), a
    inc  hl
    djnz clear_cells_loop

    ;; ------------------------------------------------------------------------
    ;; write trampoline to VRAM
    ;; ------------------------------------------------------------------------

    ld   hl, #trampoline_data
    ld   d, #0x3F
    ld   a, #3

create_trampoline_loop:
    inc  d
    ld   e, b        ;; B==0 since DJNZ or LDIR, so start of next scanline
    ld   c, #5
    ldir
    dec  a
    jr   nz, create_trampoline_loop

    ;; ------------------------------------------------------------------------
    ;; if IFF1 is 0 in snapshot, replace EI with NOP
    ;; ------------------------------------------------------------------------

    ld   a, (stored_snapshot_header + Z80_HEADER_OFFSET_IFF1)
    or   a, a
    jr   nz, evacuate_iff1_set           ;; if A is zero
    ld   (VRAM_TRAMPOLINE_EI_OR_NOP), a  ;; then it can be used as a NOP

evacuate_iff1_set:

    ;; ------------------------------------------------------------------------
    ;; write register state to VRAM trampoline area
    ;; ------------------------------------------------------------------------

    ld   hl, (stored_snapshot_header + Z80_HEADER_OFFSET_R)

    ;; now L holds low 7 bits of R, and bit 0 of H holds bit 7 of R

    ld   a, #REG_R_ADJUSTMENT
    add  a, l
    rla                ;; carry now in bit 0, but shifted again out soon
    rr   h
    rra
    ld   (VRAM_REGSTATE_R), a

    ;; ------------------------------------------------------------------------
    ;; write A to trampoline
    ;; ------------------------------------------------------------------------

    ld   hl, #stored_snapshot_header + Z80_HEADER_OFFSET_A
    ld   de, #VRAM_REGSTATE_A
    ld   bc, #6                  ;; A + F + BC + HL

    ldi                          ;; only one byte (A), BC is now 5

    ;; ------------------------------------------------------------------------
    ;; write F
    ;; ------------------------------------------------------------------------

    ;; HL now points to stored_snapshot_header + Z80_HEADER_OFFSET_F

    ld   de, #VRAM_REGSTATE_F

    ldi                          ;; only one byte (F), BC is now 4

    ;; ------------------------------------------------------------------------
    ;; write BC and HL
    ;; ------------------------------------------------------------------------

    ;; HL now points to stored_snapshot_header + Z80_HEADER_OFFSET_BC_HL

    ld   e, #<VRAM_REGSTATE_BC_HL_F
    ldir

    ld   hl, (stored_snapshot_header + Z80_HEADER_OFFSET_SP)
    ld   (VRAM_REGSTATE_SP), hl

    ;; ========================================================================
    ;; Set up 48k/128k flag, 128k memory configuration, PC value
    ;; ========================================================================

    ;; ------------------------------------------------------------------------
    ;; check snapshot version (is PC == 0?)
    ;; set up 128k flag + memory config in DE, snapshot PC in HL
    ;; ------------------------------------------------------------------------

    ld   hl, (stored_snapshot_header + Z80_HEADER_OFFSET_PC)
    ld   a, h
    or   a, l      ;; extended snapshot (version 2+) ?
    jr   nz, prepare_context_48k     ;; non-zero PC means version 1, always 48k

    ;; ------------------------------------------------------------------------
    ;; snapshot version 2+:
    ;; load HW_TYPE into E, and memory config into D
    ;; ------------------------------------------------------------------------

    ld   de, (stored_snapshot_header + Z80_HEADER_OFFSET_HW_TYPE)
    ld   hl, (stored_snapshot_header + Z80_HEADER_OFFSET_EXT_PC)

    ;; ------------------------------------------------------------------------
    ;; Check HW_TYPE: only use 128k memory config from snapshot if this is
    ;; actually a 128k snapshot
    ;; ------------------------------------------------------------------------

    ld   a, e
    cp   a, #SNAPSHOT_128K
    jr   nc, prepare_context_set_bank
prepare_context_48k:

    ;; -----------------------------------------------------------------------
    ;; Memory configuration for a 48k snapshot on a 128k machine. Bits are
    ;; set as follows:
    ;;
    ;; Bits 0..2 := 0:  page 0 at 0xC000
    ;; Bit 3     := 0:  normal screen (page 5)
    ;; Bit 4     := 1:  48k BASIC ROM
    ;; Bit 5     := 1:  lock memory paging
    ;;
    ;; https://worldofspectrum.org/faq/reference/128kreference.htm#ZX128Memory
    ;; -----------------------------------------------------------------------


    ld   de, #(MEMCFG_ROM_48K + MEMCFG_LOCK) << 8  ;; config for a 48k snapshot
prepare_context_set_bank:
    ld   (VRAM_REGSTATE_PC), hl
    ld   (memory_state), de

    ;; ========================================================================
    ;; write evacuated data to ENC28J60 RAM
    ;; ========================================================================

    ld   hl, #ENC28J60_EVACUATED_DATA
    ld   a, #OPCODE_WCR + (EWRPTL & REG_MASK)
    rst  enc28j60_write_register16

    ld   de, #RUNTIME_DATA_LENGTH
    ld   hl, #EVACUATION_TEMP_BUFFER

    call enc28j60_write_memory

    .endm