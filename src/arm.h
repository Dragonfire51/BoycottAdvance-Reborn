#ifndef ARM_H
#define ARM_H

#include "gba.h"

void arm_exec(GBA *g);          /* execute one ARM instruction */
void thumb_exec(GBA *g);        /* execute one Thumb instruction */
void arm_execute(GBA *g, uint32_t max_cycles);
void gba_add_cycles(GBA *g, uint32_t n);  /* charge emulated cycles (BIOS HLE timing) */
void gba_swi(GBA *g, uint32_t comment);   /* SVC / BIOS (comment = SWI number) */
void arm_set_mode(GBA *g, uint32_t mode);
void arm_raise_interrupt(GBA *g, int n);
void arm_set_irq_line(GBA *g, bool high);
bool arm_is_halted(GBA *g);

#endif

