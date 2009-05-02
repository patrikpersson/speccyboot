/*
 * Memory map for test application:
 * 0x4000 .. 0x6FFF   data from test1.data (12K)
 * 0x7000 ..          this application
 *        .. 0x7400   initial stack pointer (before registers are pushed)
 * 0x7400 .. 0xFFFF   data from test2.data (35K)
 *
 * This application verifies the following:
 * - Areas 0x4000..0x6FFF and 0x7400..0xFFFF were loaded correctly
 * - Registers AF, BC, DE, HL, IX, IY, AF', BC', DE', HL', SP, I, R, and PC
 *   were initialized correctly
 *
 * The following is NOT verified:
 * - Interrupt status (IFF1, IFF2, mode)
 */

#include <stdint.h>

#include "register-values.h"

#define RAM_START           (0x4000)
#define APP_START           (0x7000)
#define APP_END             (0x7400)
#define RAM_END             (0xffff)

#define EXPECTED_CHECKSUM   (0x00)

/*
 * Addresses of registers, as pushed on the stack in crt0.asm
 * (this implicitly checks that SP is correct)
 */
#define ADDR_OF_A           (0x73FF)
#define ADDR_OF_F           (0x73FE)

/* F_AFTER_R == value of F after 'ld A, R' instruction */

#define ADDR_OF_R           (0x73FD)
#define ADDR_OF_F_AFTER_R   (0x73FC)

/* F_AFTER_I == value of F after 'ld A, I' instruction */

#define ADDR_OF_I           (0x73FB)
#define ADDR_OF_F_AFTER_I   (0x73FA)
  
#define ADDR_OF_B           (0x73F9)
#define ADDR_OF_C           (0x73F8)

#define ADDR_OF_D           (0x73F7)
#define ADDR_OF_E           (0x73F6)

#define ADDR_OF_H           (0x73F5)
#define ADDR_OF_L           (0x73F4)

#define ADDR_OF_IX_HI       (0x73F3)
#define ADDR_OF_IX_LO       (0x73F2)

#define ADDR_OF_IY_HI       (0x73F1)
#define ADDR_OF_IY_LO       (0x73F0)

/*
 * Alternate register bank
 */
#define ADDR_OF_AP          (0x73EF)
#define ADDR_OF_FP          (0x73EE)

#define ADDR_OF_BP          (0x73ED)
#define ADDR_OF_CP          (0x73EC)

#define ADDR_OF_DP          (0x73EB)
#define ADDR_OF_EP          (0x73EA)

#define ADDR_OF_HP          (0x73E9)
#define ADDR_OF_LP          (0x73E8)

#define CHECK_REGISTER(name)                                            \
  if (*((uint8_t *) ADDR_OF_ ## name ) != ( REG_ ## name ))  goto fail

void main(void)
{
  uint8_t  checksum = 0;
  static sfr at 0xFE border;     /* I/O address of ULA */
  
  CHECK_REGISTER(A);
  CHECK_REGISTER(F);

  CHECK_REGISTER(B);
  CHECK_REGISTER(C);
  CHECK_REGISTER(D);
  CHECK_REGISTER(E);
  CHECK_REGISTER(H);
  CHECK_REGISTER(L);
  
  CHECK_REGISTER(IX_HI);
  CHECK_REGISTER(IX_LO);
  CHECK_REGISTER(IY_HI);
  CHECK_REGISTER(IY_LO);
  
  CHECK_REGISTER(AP);
  CHECK_REGISTER(FP);
  
  CHECK_REGISTER(BP);
  CHECK_REGISTER(CP);
  CHECK_REGISTER(DP);
  CHECK_REGISTER(EP);
  CHECK_REGISTER(HP);
  CHECK_REGISTER(LP);
  
  CHECK_REGISTER(I);
  
  /*
   * R needs a bit of care:
   * the value stored equals (R + 3), since R increases automatically
   */
  {
    uint8_t r_value = *((uint8_t *) ADDR_OF_R);
    if (r_value - 3 != REG_R)  goto fail;
  }

  /*
   * Check RAM contents
   */
  {
    uint16_t addr;
    for (addr = RAM_START; addr < APP_START; addr++) {
      checksum += *((const uint8_t *) addr);
    }
    for (addr = APP_END; addr < RAM_END; addr++) {
      checksum += *((const uint8_t *) addr);
    }
  }
  
  if (checksum != EXPECTED_CHECKSUM)  goto fail;
  
  border = 4;     /* green */
  goto stop;
  
fail:
  border = 2;     /* red */
  
stop:
  __asm
    di
    halt
  __endasm;
}