#ifndef MONAC_LEXER_JSON_H
#define MONAC_LEXER_JSON_H

#include "lexer.h"

enum {
    MC_JSON_STATE_NORMAL = 0,
    MC_JSON_STATE_IN_STRING = 1,
    MC_JSON_STATE_IN_BLOCK_COMMENT = 2
};

const mc_lexer_vtable_t *mc_lexer_json_vtable(void);

#endif /* MONAC_LEXER_JSON_H */
