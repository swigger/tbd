//
//  src/arch_info.c
//  tbd
//
//  Created by inoahdev on 11/19/18.
//  Copyright © 2018 - 2019 inoahdev. All rights reserved.
//

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "mach/machine.h"

#include "array.h"
#include "arch_info.h"

/*
 * To support the use of fake-arrays, we don't const our arch-info table.
 */

static struct arch_info arch_info_list[] = {
    { CPU_TYPE_ANY, CPU_SUBTYPE_MULTIPLE,      "any"    },
    { CPU_TYPE_ANY, CPU_SUBTYPE_LITTLE_ENDIAN, "little" },
    { CPU_TYPE_ANY, CPU_SUBTYPE_BIG_ENDIAN,    "big"    },

    /*
     * Index starts at 3 and ends at 5.
     */

    { CPU_TYPE_MC680x0, CPU_SUBTYPE_MC680x0_ALL,  "m68k"   },
    { CPU_TYPE_MC680x0, CPU_SUBTYPE_MC68040,      "m68040" },
    { CPU_TYPE_MC680x0, CPU_SUBTYPE_MC68030_ONLY, "m68030" },

    /*
     * Index starts at 6 and ends at 14.
     */

    { CPU_TYPE_X86, CPU_SUBTYPE_I386_ALL,  "i386"     },
    { CPU_TYPE_X86, CPU_SUBTYPE_486,       "i486"     },
    { CPU_TYPE_X86, CPU_SUBTYPE_486SX,     "i486SX"   },
    { CPU_TYPE_X86, CPU_SUBTYPE_PENT,      "pentium"  },
    { CPU_TYPE_X86, CPU_SUBTYPE_PENTPRO,   "pentpro"  },
    { CPU_TYPE_X86, CPU_SUBTYPE_PENTII_M3, "pentIIm3" },
    { CPU_TYPE_X86, CPU_SUBTYPE_PENTII_M5, "pentIIm5" },
    { CPU_TYPE_X86, CPU_SUBTYPE_PENTIUM_4, "pentium4" },
    { CPU_TYPE_X86, CPU_SUBTYPE_X86_64_H,  "x86_64h"  },

    /*
     * Index starts at 15 and ends at 16.
     */

    { CPU_TYPE_HPPA, CPU_SUBTYPE_HPPA_ALL,  "hppa"       },
    { CPU_TYPE_HPPA, CPU_SUBTYPE_HPPA_7100, "hppa7100LC" },

    /*
     * Index starts at 17 and ends at 29.
     */

    { CPU_TYPE_ARM, CPU_SUBTYPE_ARM_ALL,    "arm"     },
    { CPU_TYPE_ARM, CPU_SUBTYPE_ARM_V4T,    "armv4t"  },
    { CPU_TYPE_ARM, CPU_SUBTYPE_ARM_V6,     "armv6"   },
    { CPU_TYPE_ARM, CPU_SUBTYPE_ARM_V5TEJ,  "armv5"   },
    { CPU_TYPE_ARM, CPU_SUBTYPE_ARM_XSCALE, "xscale"  },
    { CPU_TYPE_ARM, CPU_SUBTYPE_ARM_V7,     "armv7"   },
    { CPU_TYPE_ARM, CPU_SUBTYPE_ARM_V7F,    "armv7f"  },
    { CPU_TYPE_ARM, CPU_SUBTYPE_ARM_V7S,    "armv7s"  },
    { CPU_TYPE_ARM, CPU_SUBTYPE_ARM_V7K,    "armv7k"  },
    { CPU_TYPE_ARM, CPU_SUBTYPE_ARM_V6M,    "armv6"   },
    { CPU_TYPE_ARM, CPU_SUBTYPE_ARM_V7M,    "armv7m"  },
    { CPU_TYPE_ARM, CPU_SUBTYPE_ARM_V7EM,   "armv7em" },
    { CPU_TYPE_ARM, CPU_SUBTYPE_ARM_V8,     "armv8"   },

    /*
     * Following's index is 30.
     */

    { CPU_TYPE_MC88000, CPU_SUBTYPE_MC88000_ALL, "m88k" },

    /*
     * Following's index is 31.
     */

    { CPU_TYPE_SPARC, CPU_SUBTYPE_SPARC_ALL, "sparc" },

    /*
     * Following's index is 32.
     */

    { CPU_TYPE_I860, CPU_SUBTYPE_I860_ALL, "i860" },

    /*
     * Index starts at 33 and ends at 44.
     */

    { CPU_TYPE_POWERPC, CPU_SUBTYPE_POWERPC_ALL,   "ppc"      },
    { CPU_TYPE_POWERPC, CPU_SUBTYPE_POWERPC_601,   "ppc601"   },
    { CPU_TYPE_POWERPC, CPU_SUBTYPE_POWERPC_602,   "ppc602"   },
    { CPU_TYPE_POWERPC, CPU_SUBTYPE_POWERPC_603,   "ppc603"   },
    { CPU_TYPE_POWERPC, CPU_SUBTYPE_POWERPC_603e,  "ppc603e"  },
    { CPU_TYPE_POWERPC, CPU_SUBTYPE_POWERPC_603ev, "ppc603ev" },
    { CPU_TYPE_POWERPC, CPU_SUBTYPE_POWERPC_604,   "ppc604"   },
    { CPU_TYPE_POWERPC, CPU_SUBTYPE_POWERPC_604e,  "ppc604e"  },
    { CPU_TYPE_POWERPC, CPU_SUBTYPE_POWERPC_750,   "ppc750"   },
    { CPU_TYPE_POWERPC, CPU_SUBTYPE_POWERPC_7400,  "ppc7400"  },
    { CPU_TYPE_POWERPC, CPU_SUBTYPE_POWERPC_7450,  "ppc7450"  },
    { CPU_TYPE_POWERPC, CPU_SUBTYPE_POWERPC_970,   "ppc970"   },

    /*
     * Index starts at 45 and ends at 47.
     */

    { CPU_TYPE_VEO, CPU_SUBTYPE_VEO_ALL, "veo"  },
    { CPU_TYPE_VEO, CPU_SUBTYPE_VEO_1,   "veo1" },
    { CPU_TYPE_VEO, CPU_SUBTYPE_VEO_2,   "veo2" },

    /*
     * Index starts at 48 and ends at 49.
     */

    { CPU_TYPE_X86_64, CPU_SUBTYPE_X86_64_ALL, "x86_64"  },
    { CPU_TYPE_X86_64, CPU_SUBTYPE_X86_64_H,   "x86_64h" },

    /*
     * Index starts from 50 and ends at 52.
     */

    { CPU_TYPE_ARM64, CPU_SUBTYPE_ARM64_ALL, "arm64"  },
    { CPU_TYPE_ARM64, CPU_SUBTYPE_ARM64_V8,  "arm64"  },
    { CPU_TYPE_ARM64, CPU_SUBTYPE_ARM64E,    "arm64e" },

    /*
     * Index starts at 53 and ends at 54.
     */

    { CPU_TYPE_POWERPC64, CPU_SUBTYPE_POWERPC_ALL, "ppc64"     },
    { CPU_TYPE_POWERPC64, CPU_SUBTYPE_POWERPC_970, "ppc970-64" },

    /*
     * Following's index is 55.
     */

    { CPU_TYPE_ARM64_32, CPU_SUBTYPE_ARM64_ALL, "arm64_32" },

    /*
     * Following's index is 56.
     */

    { 0, 0, NULL }
};

/*
 * Create a fake array to pass binary-search onto array's functions.
 */

static const struct array arch_info_array = {
    .data = (void *)arch_info_list,
    .data_end = (void *)(arch_info_list + 55),
    .alloc_end = (void *)(arch_info_list + 55)
};

struct arch_info_cputype_info {
    cpu_type_t cputype;

    uint64_t front;
    uint64_t back;
};

/*
 * To support the use of fake-arrays, we don't const our cputype-info table.
 */

static struct arch_info_cputype_info cputype_info_list[] = {
    { CPU_TYPE_ANY,        0, 2  },
    { CPU_TYPE_MC680x0,    3, 5  },
    { CPU_TYPE_X86,        6, 14 },
    { CPU_TYPE_HPPA,      15, 16 },
    { CPU_TYPE_ARM,       17, 29 },
    { CPU_TYPE_MC88000,   30, 30 },
    { CPU_TYPE_SPARC,     31, 31 },
    { CPU_TYPE_I860,      32, 32 },
    { CPU_TYPE_POWERPC,   33, 44 },
    { CPU_TYPE_VEO,       45, 47 },
    { CPU_TYPE_X86_64,    48, 49 },
    { CPU_TYPE_ARM64,     50, 52 },
    { CPU_TYPE_POWERPC64, 53, 54 },
    { CPU_TYPE_ARM64_32,  55, 55 }
};

/*
 * Create a fake array to pass binary-search onto array's functions.
 */

const struct array cputype_info_array = {
    .data = (void *)cputype_info_list,
    .data_end = (void *)(cputype_info_list + 14),
    .alloc_end = (void *)(cputype_info_list + 14)
};

const struct arch_info *arch_info_get_list(void) {
    return arch_info_list;
}

uint64_t arch_info_list_get_size(void) {
    return sizeof(arch_info_list) / sizeof(struct arch_info);
}

static uint64_t cputype_info_list_get_size(void) {
    return sizeof(cputype_info_list) / sizeof(struct arch_info_cputype_info);
}

static int
cputype_info_comparator(const void *const left, const void *const right) {
    const struct arch_info *const info = (const struct arch_info *)left;
    const cpu_type_t cputype = *(const cpu_type_t *)right;

    return info->cputype - cputype;
}

static int
arch_info_cpusubtype_comparator(const void *const left, const void *const right)
{
    const struct arch_info *const info = (const struct arch_info *)left;
    const cpu_subtype_t cpusubtype = *(const cpu_subtype_t *)right;

    return info->cpusubtype - cpusubtype;
}

const struct arch_info *
arch_info_for_cputype(const cpu_type_t cputype, const cpu_subtype_t cpusubtype)
{
    /*
     * First find the cputype-info for the cputype provided to get the range
     * inside arch_info_list, where we will then search within for the right
     * arch.
     */

    const struct array_slice cputype_info_slice = {
        .front = 0,
        .back = cputype_info_list_get_size() - 1
    };

    const struct arch_info_cputype_info *const info =
        array_find_item_in_sorted_with_slice(
            &cputype_info_array,
            sizeof(struct arch_info_cputype_info),
            cputype_info_slice,
            &cputype,
            cputype_info_comparator,
            NULL);

    if (info == NULL) {
        return NULL;
    }

    const uint64_t info_front_index = info->front;
    if (info_front_index == info->back) {
        const struct arch_info *const arch = arch_info_list + info_front_index;
        if (arch->cpusubtype == cpusubtype) {
            return arch;
        }

        return NULL;
    }

    const struct array_slice slice = {
        .front = info->front,
        .back = info->back
    };

    const struct arch_info *const arch =
        array_find_item_in_sorted_with_slice(&arch_info_array,
                                             sizeof(struct arch_info),
                                             slice,
                                             &cpusubtype,
                                             arch_info_cpusubtype_comparator,
                                             NULL);

    if (arch == NULL) {
        return NULL;
    }

    return arch;
}

const struct arch_info *arch_info_for_name(const char *const name) {
    const struct arch_info *arch = arch_info_list;
    for (; arch->name != NULL; arch++) {
        if (strcmp(arch->name, name) != 0) {
            continue;
        }

        return arch;
    }

    return NULL;
}
