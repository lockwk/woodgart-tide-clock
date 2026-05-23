#ifndef ICON_TYPES_H
#define ICON_TYPES_H

#include <stdint.h>

/* Shared icon descriptor — used by all icon_*.h headers and layout.c */
typedef struct {
    const uint8_t *table;  /* packed 2-bit 4-gray bitmap, 4px/byte MSB-first */
    uint16_t       Width;  /* original pixel width (unpacked)                 */
    uint16_t       Height; /* pixel height                                    */
} sICON;

#endif /* ICON_TYPES_H */
