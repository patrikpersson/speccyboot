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
;; 0x5B00 .. 0x5FFF  1280B  stack, data                                  (!)
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
;; Code copied to VRAM for trampoline
;; ----------------------------------------------------------------------------
    .globl trampoline_data

;; ----------------------------------------------------------------------------
;; Return address for SPI read loop in PERFORM_CONTEXT_SWITCH
;; (used for a one-word, ROM-based stack in init.asm)
;; ----------------------------------------------------------------------------
    .globl context_switch_spi_return

;; ----------------------------------------------------------------------------
;; Stack pointer value (top of stack) for one-word, ROM-based stack in init.asm
;; ----------------------------------------------------------------------------
    .globl spi_restore_stack_top

;; ============================================================================
;; Macro: prepare context switch
;;
;; Write trampoline to (what will be) VRAM,
;; store register values,
;; copy runtime data to ENC28J60 SRAM
;; ============================================================================

    .macro  SETUP_CONTEXT_SWITCH

    ;; ========================================================================
    ;; Prepare VRAM trampoline.
    ;;
    ;; Clear out the top-left five character cells, by setting ink colour
    ;; to the same as the paper colour.
    ;;
    ;; (These character cells are used as temporary storage for the trampoline
    ;; below.)
    ;; ========================================================================

    ld   de, #EVACUATION_TEMP_BUFFER + 5            ;; points to attribute data
    ld   b, #5
clear_cells_loop:
    dec  e
    ld   a, (de)
    rra
    rra
    rra
    and  a, #7
    ld   c, a
    ld   a, (de)
    and  a, #0xf8
    or   a, c
    ld   (de), a
    djnz clear_cells_loop

    ;; -----------------------------------------------------------------------
    ;; write trampoline to VRAM
    ;; -----------------------------------------------------------------------

    ld   hl, #trampoline_data
    ld   d, #0x3F                                                 ;; E==0 here

create_trampoline_loop:
    inc  d
    ld   c, #5                      ;; B == 0 from DJNZ above or previous LDIR
    ldir
    ld   e, b                  ;; B == 0 since LDIR, so start of next scanline
    bit  1, d                                      ;; up to and including 0x42
    jr   z, create_trampoline_loop

    ;; -----------------------------------------------------------------------
    ;; if IFF1 is 0 in snapshot, replace EI with NOP
    ;; -----------------------------------------------------------------------

    ld   a, (stored_snapshot_header + Z80_HEADER_OFFSET_IFF1)
    or   a, a
    jr   nz, evacuate_iff1_set
    ld   (VRAM_TRAMPOLINE_EI_OR_NOP), a  ;; and can be used as a NOP

evacuate_iff1_set:

    ;; ------------------------------------------------------------------------
    ;; write register state to VRAM trampoline area
    ;; ------------------------------------------------------------------------

    ld   hl, #stored_snapshot_header + Z80_HEADER_OFFSET_R

    ld   a, (hl)
    add  a, #REG_R_ADJUSTMENT
    rla                ;; carry now in bit 0, but shifted again out soon
    inc  hl            ;; stored_snapshot_header + Z80_HEADER_OFFSET_MISC_FLAGS
    rr   (hl)          ;; get bit 7 from flags, shift border bits into place
    rra                ;; and shift it into A
    ld   (VRAM_REGSTATE_R), a

    ;; ------------------------------------------------------------------------
    ;; Write A to trampoline.
    ;;
    ;; BC is set up for the LDIR below. To ensure that the two LDI instructions
    ;; don't make B wrap around, C is given an appropriate value already here.
    ;; ------------------------------------------------------------------------

    ld   l, #<stored_snapshot_header + Z80_HEADER_OFFSET_A
    ld   de, #VRAM_REGSTATE_A
    ld   c, #6                   ;; B==0 here; set counter for A + F + BC + HL

    ldi                          ;; only one byte (A), BC is now 5

    ;; ------------------------------------------------------------------------
    ;; write F
    ;; ------------------------------------------------------------------------

    ;; HL now points to Z80_HEADER_OFFSET_F

    ld   de, #VRAM_REGSTATE_F

    ldi                          ;; only one byte (F), BC is now 4

    ;; ------------------------------------------------------------------------
    ;; write BC, HL, SP, PC
    ;; ------------------------------------------------------------------------

    ;; HL now points to Z80_HEADER_OFFSET_BC_HL

    ld   e, b       ;; <VRAM_REGSTATE_BC_HL_F ; B==0 here
    ldir

    ld   hl, (stored_snapshot_header + Z80_HEADER_OFFSET_SP)
    ld   (VRAM_REGSTATE_SP), hl

    ;; Z80_HEADER_OFFSET_EXT_PC is the field for v.2+ snapshots. For v.1
    ;; snapshots, it is explicitly set in z80_loader (state s_header), so it
    ;; is always valid here, regardless of snapshot version.

    ld   hl, (stored_snapshot_header + Z80_HEADER_OFFSET_EXT_PC)
    ld   (VRAM_REGSTATE_PC), hl

    ;; ========================================================================
    ;; write evacuated data to ENC28J60 RAM
    ;; ========================================================================

    ld   hl, #ENC28J60_EVACUATED_DATA
    ld   a, #OPCODE_WCR | EWRPTL
    rst  enc28j60_write_register16

    ld   de, #RUNTIME_DATA_LENGTH
    ld   hl, #EVACUATION_TEMP_BUFFER

    call enc28j60_write_memory

    .endm

;; ============================================================================
;; Macro: perform final context switch to loaded application
;;
;; Restore application data from ENC28J60 SRAM, restore register values
;; and system state from the stored .z80 header, and execute the application.
;; ============================================================================

    .macro  PERFORM_CONTEXT_SWITCH

    di

    ;; ------------------------------------------------------------------------
    ;; set ERDPT := ENC28J60_EVACUATED_DATA
    ;; ------------------------------------------------------------------------

    ld   hl, #ENC28J60_EVACUATED_DATA
    ld   a, #OPCODE_WCR | ERDPTL
    rst  enc28j60_write_register16

    ;; ------------------------------------------------------------------------
    ;; Now D == original L == <ENC28J60_EVACUATED_DATA == 0x00
    ;;
    ;; Set D := 0xff, high byte of SND_REG_SELECT
    ;; ------------------------------------------------------------------------

    dec  d    ;; D := 0xff

    ;; ------------------------------------------------------------------------
    ;; set up 128k memory configuration
    ;; ------------------------------------------------------------------------

    ld   hl, #ram_config
    ld   bc, #MEMCFG_ADDR
    outd                      ;; write 128k memory config

    ;; ------------------------------------------------------------------------
    ;; check whether this is a 48k or 128k snapshot
    ;; ------------------------------------------------------------------------

    ;; HL now points to kilobytes_expected (decreased by OUTD)

    bit  7, (hl)
    jr   z, context_switch_48k_snapshot

    ;; ------------------------------------------------------------------------
    ;; 128k snapshot: restore sound registers, in reverse order
    ;; (makes index equal to loop counter, and ensures HL points to
    ;; something useful afterwards).
    ;; ------------------------------------------------------------------------

    ld   a, #16
    ld   l, #<stored_snapshot_header + Z80_HEADER_OFFSET_HW_STATE_SND + 15

context_switch_snd_reg_loop:
    dec  a
    ld   b, d                       ;; BC is now SND_REG_SELECT
    out  (c), a                     ;; write A to SND_REG_SELECT
    ld   b, #>SND_REG_VALUE + 1     ;; +1 because OUTD decreases B
    outd                            ;; write (HL) to SND_REG_VALUE
    or   a, a
    jr   nz, context_switch_snd_reg_loop

    ;; ------------------------------------------------------------------------
    ;; Restore value of SND_REG_SELECT (Z80_HEADER_OFFSET_HW_STATE_FFFD)
    ;; ------------------------------------------------------------------------

    ;; HL now points to Z80_HEADER_OFFSET_HW_STATE_FFFD. Set BC to
    ;; 0xFFFD == SND_REG_SELECT, but compensate for the fact that OUTD
    ;; decreases B before the bus access. Set B to 0, to be decreased to
    ;; 0xFF == >SND_REG_SELECT by OUTD. A == 0 after JR NZ above.

    ld   b, a
    outd

context_switch_48k_snapshot:

    ;; ------------------------------------------------------------------------
    ;; Set up SP for popping state from stored_snapshot_header,
    ;; restore I
    ;; ------------------------------------------------------------------------

    ld   sp, #stored_snapshot_header + Z80_HEADER_OFFSET_I - 1

    pop  af          ;; A := I, F := high byte of SP (ignore)
    ld   i, a

    ;; -----------------------------------------------------------------------
    ;; Restore border. The border bits are nominally bit 1..3 in MISC_FLAGS,
    ;; but this byte was rotated one bit above (to obtain bit for R), so
    ;; no shifting is needed here.
    ;;
    ;; This shifting also means that the other bits from MISC_FLAGS will be
    ;; shifted into the ULA output value:
    ;;
    ;; bit 3 (MIC)  --  set to 1 if Basic SamRom switched in
    ;; bit 4 (EAR)  --  set to 1 for a v1 snapshot with compressed data
    ;;
    ;; Some ROM space is saved by not masking out these. In practice,
    ;; it doesn't matter beyond a slight loudspeaker click when loading some
    ;; v1 snapshots.
    ;; -----------------------------------------------------------------------

    pop  af                                ;; A := MISC_FLAGS, F := R (ignore)
    out  (ULA_PORT), a                           ;; saved an AND A, #0x07 here

    ;; -----------------------------------------------------------------------
    ;; Restore the following registers early, to avoid using VRAM for them:
    ;; - DE
    ;; - alternate registers (BC, DE, HL, AF)
    ;; - IX & IY
    ;; -----------------------------------------------------------------------

    pop    de
    exx
    pop    bc
    pop    de
    pop    hl
    exx

    ;; the .z80 snapshot format has switched A and F around, so some
    ;; trickery is required to restore AF'

    ;; A goes first (loaded temporarily to C)
    pop    bc

    ;; then F
    dec    sp
    pop    af
    dec    sp

    ld     a, c
    ex     af, af'

    pop    iy
    pop    ix

    ;; ------------------------------------------------------------------------
    ;; Restore interrupt mode
    ;; ------------------------------------------------------------------------

    inc  sp            ;; skip Z80_HEADER_OFFSET_IFF1
    pop  af            ;; pop F := IFF2 state, A := Z80_HEADER_OFFSET_INT_MODE

    im   1
    rra                              ;; bit 0 set means IM 1 (there is no IM 3)
    jr   c, context_switch_im_set
    im   2
    rra                              ;; bit 1 set means IM 2 (same reason)
    jr   c, context_switch_im_set
    im   0

context_switch_im_set:

    ;; ========================================================================
    ;; Restore application data temporarily stored in ENC28J60 RAM.
    ;; Use stored_snapshot_header as stack for spi_write_byte, and
    ;; a minimal, read-only stack in ROM for the following data bytes.
    ;;
    ;; ERDPT was set to ENC28J60_EVACUATED_DATA above.
    ;; ========================================================================

    ld    c, #OPCODE_RBM
    rst   spi_write_byte

    ;; ------------------------------------------------------------------------
    ;; read RUNTIME_DATA_LENGTH bytes from current ERDPT to RUNTIME_DATA
    ;; ------------------------------------------------------------------------

    ld    hl, #RUNTIME_DATA
    ld    sp, #spi_restore_stack_top
context_switch_restore_bytes_loop:

    call  spi_read_byte_to_c

context_switch_spi_return:               ;; return address for ROM-based stack
                                         ;; (see spi_restore_stack_top)

    ld    (hl), c
    inc   hl
    ld    a, h
    sub   a, #>(RUNTIME_DATA + RUNTIME_DATA_LENGTH) ;; integral number of pages
    jr    nz, context_switch_restore_bytes_loop

    ;; ------------------------------------------------------------------------
    ;; we're done with the ENC28J60: put it in the reset state
    ;; ------------------------------------------------------------------------

    ;; A == 0 from SUB A, #n above

    out (SPI_OUT), a

    ;; ------------------------------------------------------------------------
    ;; Restore BC, HL, F
    ;; ------------------------------------------------------------------------

    ld    sp, #VRAM_REGSTATE_BC_HL_F
    pop   bc
    pop   hl
    pop   af        ;; A gets wrong value here, but this is fixed in trampoline

    ;; ------------------------------------------------------------------------
    ;; Restore SP and R
    ;; ------------------------------------------------------------------------

    ld    sp, (VRAM_REGSTATE_SP)

    ld    a, (VRAM_REGSTATE_R)
    ld    r, a

    ;; ------------------------------------------------------------------------
    ;; Set up final register state for trampoline
    ;; ------------------------------------------------------------------------

    ld    a, #0x20      ;; page out SpeccyBoot, keep ENC28J60 in reset

    jp    VRAM_TRAMPOLINE_START

    .endm
