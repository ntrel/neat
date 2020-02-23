#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "symbol.h"

int declare(Environment *environment, Symbol *symbol) {
    printf("declare symbol %s at %i (%i)\n", symbol->name, environment->entries.length, symbol->args_len);
    environment->entries.ptr = realloc(
        environment->entries.ptr, ++environment->entries.length * sizeof(SymbolEntry));
    environment->entries.ptr[environment->entries.length - 1] = (SymbolEntry) {
        .kind = UNRESOLVED_SYMBOL,
        .symbol = symbol
    };
    return environment->entries.length - 1;
}

SymbolEntry *find_symbol(Environment *environment, const char *name) {
    for (int i = 0; i < environment->entries.length; i++) {
        SymbolEntry *entry = &environment->entries.ptr[i];

        if (strcmp(entry->symbol->name, name) == 0) {
            return entry;
        }
    }
    fprintf(stderr, "no such symbol: %s\n", name);
    assert(false);
    abort();
}

void resolve_c(Environment *environment, const char *name, callptr_t callptr) {
    // loop manually; intrinsics may be multiply defined (TODO assert this isn't the case)
    for (int i = 0; i < environment->entries.length; i++) {
        SymbolEntry *entry = &environment->entries.ptr[i];

        if (strcmp(entry->symbol->name, name) == 0) {
            entry->kind = C_SYMBOL;
            entry->callptr = callptr;
        }
    }
}

void resolve_bc(Environment *environment, const char *name, DefineSection *define_section) {
    SymbolEntry *entry = find_symbol(environment, name);
    const char *symbol_name = entry->symbol->name;
    entry->kind = BC_SYMBOL;
    entry->arg_types = (Type*) WORD_ALIGN((size_t)(symbol_name + strlen(symbol_name) + 1));
    entry->section = define_section;
}
