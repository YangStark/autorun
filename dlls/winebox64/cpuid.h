/* Copyright 2026 Wine-NX contributors. LGPL-2.1-or-later. */
#ifndef WINEBOX64_CPUID_H
#define WINEBOX64_CPUID_H

/* The x86 CPU the interpreter presents, in one place: its CPUID leaf 1 answer
 * (wow64_box64_engine.c), BTCpuIsProcessorFeaturePresent and the processor
 * information WoW64 gives 32-bit programs (GetSystemInfo). A conservative
 * Pentium 4 class CPU: only features whose instructions the interpreter runs
 * and whose state crosses runs (x87, MMX, SSE and SSE2 through FXSAVE). */
#define WINEBOX64_CPUID_SIGNATURE 0x00000f29 /* family 15, model 2, stepping 9 */
#define WINEBOX64_CPUID_EDX ((1u << 0) /* FPU */ | (1u << 4) /* TSC */ | (1u << 8) /* CX8 */ | \
                             (1u << 15) /* CMOV */ | (1u << 23) /* MMX */ | (1u << 24) /* FXSR */ | \
                             (1u << 25) /* SSE */ | (1u << 26) /* SSE2 */)

static inline BOOLEAN winebox64_x86_feature_present( UINT feature )
{
    switch (feature)
    {
    case PF_COMPARE_EXCHANGE_DOUBLE:        /* CX8 */
    case PF_MMX_INSTRUCTIONS_AVAILABLE:
    case PF_XMMI_INSTRUCTIONS_AVAILABLE:    /* SSE */
    case PF_RDTSC_INSTRUCTION_AVAILABLE:
    case PF_XMMI64_INSTRUCTIONS_AVAILABLE:  /* SSE2 */
        return TRUE;
    default:
        return FALSE;
    }
}

/* SYSTEM_CPU_INFORMATION for 32-bit programs, as Wine's i386 build derives it
 * from CPUID and the PF_* features (system.c init_cpu_model/get_cpu_features).
 * MaximumProcessors stays the host's count. */
static inline void winebox64_x86_processor_information( SYSTEM_CPU_INFORMATION *info )
{
    const unsigned int signature = WINEBOX64_CPUID_SIGNATURE;
    unsigned int family = (signature >> 8) & 0xf, model = (signature >> 4) & 0xf;
    ULONG features = 0x00000275; /* vme | pge | pse | mtrr */

    if (family == 0xf) family += (signature >> 20) & 0xff;
    if (family == 6 || family >= 0xf) model += ((signature >> 12) & 0xf) << 4;
    if (winebox64_x86_feature_present( PF_RDTSC_INSTRUCTION_AVAILABLE ))   features |= 0x00000002; /* tsc */
    if (winebox64_x86_feature_present( PF_COMPARE_EXCHANGE_DOUBLE ))       features |= 0x00000080; /* cx8 */
    if (winebox64_x86_feature_present( PF_MMX_INSTRUCTIONS_AVAILABLE ))    features |= 0x00000100; /* mmx */
    if (winebox64_x86_feature_present( PF_XMMI_INSTRUCTIONS_AVAILABLE ))   features |= 0x00042800; /* sse | fxsr | clfsh */
    if (winebox64_x86_feature_present( PF_XMMI64_INSTRUCTIONS_AVAILABLE )) features |= 0x00010000; /* sse2 */
    features |= 0x08000000; /* GenuineIntel */

    info->ProcessorArchitecture = PROCESSOR_ARCHITECTURE_INTEL;
    info->ProcessorLevel = family;
    info->ProcessorRevision = (model << 8) | (signature & 0xf);
    info->ProcessorFeatureBits = features;
}

#endif
