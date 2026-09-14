/*
 * Emulation of an AArch64 load from a window of the address space whose bytes
 * live elsewhere.
 *
 * Horizon does not always let KUSER_SHARED_DATA sit at its Windows address, so
 * virtual_alloc_first_teb moves it. Wine follows the moved pointer, but a program
 * can read 0x7ffe0000 directly; the fault handler in horizon.c serves such a read
 * from the real page with this.
 */

#ifndef __WINE_HORIZON_READ_REDIRECT_H
#define __WINE_HORIZON_READ_REDIRECT_H

#include <string.h>

struct horizon_read_regs
{
    unsigned long long x[31];   /* x0-x30 */
    unsigned long long sp;
    unsigned long long pc;
    unsigned __int128 v[32];    /* q0-q31 */
};

static inline long long horizon_read_sext( unsigned long long value, unsigned int bits )
{
    return (long long)(value << (64 - bits)) >> (64 - bits);
}

/* Emulates the load at regs->pc, instruction insn, when everything it reads lies
 * in [window, window + size): the bytes come from backing, the target registers
 * and any writeback are updated and pc moves past it. Returns 0, changing
 * nothing, for anything else - stores, other instructions, reads reaching outside
 * the window - so that the fault stays a fault. */
static inline int horizon_redirect_read( struct horizon_read_regs *regs, unsigned int insn,
                                         unsigned long long window, const unsigned char *backing,
                                         unsigned long long size )
{
    unsigned int t = insn & 31, n = (insn >> 5) & 31, t2 = (insn >> 10) & 31;
    unsigned int simd = (insn >> 26) & 1, opc = (insn >> 22) & 3, count = 1, bytes, sign = 0, i;
    unsigned long long base = n == 31 ? regs->sp : regs->x[n], address = 0, offset;
    long long imm = 0;
    int writeback = 0;

    if ((insn & 0x3a400000) == 0x28400000)  /* LDP, LDPSW */
    {
        unsigned int kind = insn >> 30, index = (insn >> 23) & 3;

        if (kind == 3 || !index || t == t2) return 0;
        bytes = simd ? 4u << kind : (kind == 2 ? 8 : 4);
        if (!simd && kind == 1) sign = 64;
        count = 2;
        imm = horizon_read_sext( (insn >> 15) & 0x7f, 7 ) * (long long)bytes;
        address = index == 1 ? base : base + imm;  /* post-index reads at the base */
        writeback = index != 2;
        if (writeback && !simd && n != 31 && (n == t || n == t2)) return 0;
    }
    else if ((insn & 0x3a000000) == 0x38000000)  /* LDR, LDUR and their variants */
    {
        unsigned int log2 = insn >> 30;

        if (simd)
        {
            if (opc == 1) bytes = 1u << log2;
            else if (opc == 3 && !log2) bytes = 16;
            else return 0;
        }
        else
        {
            if (!opc || (opc == 2 && log2 == 3) || (opc == 3 && log2 >= 2)) return 0;  /* STR, PRFM */
            bytes = 1u << log2;
            if (opc == 2) sign = 64;
            else if (opc == 3) sign = 32;
        }
        if (insn & 0x01000000)  /* unsigned scaled offset */
            address = base + ((insn >> 10) & 0xfff) * (unsigned long long)bytes;
        else if (!(insn & 0x00200000))  /* 9-bit offset: unscaled, post-index, unprivileged, pre-index */
        {
            unsigned int mode = (insn >> 10) & 3;

            if (simd && mode == 2) return 0;
            imm = horizon_read_sext( (insn >> 12) & 0x1ff, 9 );
            address = mode == 1 ? base : base + imm;
            writeback = mode == 1 || mode == 3;
            if (writeback && !simd && n != 31 && n == t) return 0;
        }
        else if (((insn >> 10) & 3) == 2)  /* register offset */
        {
            unsigned int m = (insn >> 16) & 31, option = (insn >> 13) & 7;
            unsigned long long index = m == 31 ? 0 : regs->x[m];

            if (option == 2) index = (unsigned int)index;
            else if (option == 6) index = horizon_read_sext( index, 32 );
            else if (option != 3 && option != 7) return 0;
            if (insn & 0x1000) index *= bytes;
            address = base + index;
        }
        else return 0;
    }
    else return 0;

    offset = address - window;
    if (offset >= size || size - offset < (unsigned long long)bytes * count) return 0;

    for (i = 0; i < count; i++)
    {
        const unsigned char *src = backing + offset + i * bytes;
        unsigned int target = i ? t2 : t;

        if (simd)
        {
            unsigned __int128 value = 0;

            memcpy( &value, src, bytes );  /* clears the rest of the register, as a load does */
            regs->v[target] = value;
        }
        else
        {
            unsigned long long value = 0;

            memcpy( &value, src, bytes );
            if (sign) value = horizon_read_sext( value, bytes * 8 );
            if (sign == 32) value = (unsigned int)value;
            if (target != 31) regs->x[target] = value;  /* else the zero register */
        }
    }
    if (writeback)
    {
        if (n == 31) regs->sp = base + imm;
        else regs->x[n] = base + imm;
    }
    regs->pc += 4;
    return 1;
}

#endif /* __WINE_HORIZON_READ_REDIRECT_H */
