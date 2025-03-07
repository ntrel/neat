#include <dlfcn.h>
#include <errno.h>
#ifdef __GLIBC__
#include <execinfo.h>
#endif
#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

struct String
{
    size_t length;
    char *ptr;
    void *base;
};

struct StringArray
{
    size_t length;
    struct String *ptr;
    void *base;
};

struct String string_alloc(size_t length) {
    void *memory = malloc(sizeof(size_t) * 3 + length);
    ((size_t*) memory)[0] = 1; // references
    ((size_t*) memory)[1] = length; // capacity
    ((size_t*) memory)[2] = length; // used
    return (struct String) { length, memory + sizeof(size_t) * 3, memory };
}

void neat_runtime_lock_stdout(void);
void neat_runtime_unlock_stdout(void);

void print(struct String str) {
    neat_runtime_lock_stdout();
    printf("%.*s\n", (int) str.length, str.ptr);
    fflush(stdout);
    neat_runtime_unlock_stdout();
}

void assert(int test) {
    if (!test) {
        fprintf(stderr, "Assertion failed! Aborting.\n");
        exit(1);
    }
}
int neat_runtime_ptr_test(void* ptr) { return !!ptr; }
int _arraycmp(void* a, void* b, size_t la, size_t lb, size_t sz) {
    if (la != lb) return 0;
    return memcmp(a, b, la * sz) == 0;
}
char* toStringz(struct String str) {
    char *buffer = malloc(str.length + 1);
    strncpy(buffer, str.ptr, str.length);
    buffer[str.length] = 0;
    return buffer;
}

FILE* neat_runtime_stdout() {
    return stdout;
}

void neat_runtime_system(struct String command) {
    char *cmd = toStringz(command);
    int ret = system(cmd);
    if (ret != 0) fprintf(stderr, "command failed with %i\n", ret);
    assert(ret == 0);
    free(cmd);
}

int neat_runtime_system_iret(struct String command) {
    char *cmd = toStringz(command);
    int ret = system(cmd);
    free(cmd);
    return ret;
}

int neat_runtime_execbg(struct String command, struct StringArray arguments) {
    int ret = fork();
    if (ret != 0) return ret;
    char *cmd = toStringz(command);
    char **args = malloc(sizeof(char*) * (arguments.length + 2));
    args[0] = cmd;
    for (int i = 0; i < arguments.length; i++) {
        args[1 + i] = toStringz(arguments.ptr[i]);
    }
    args[1 + arguments.length] = NULL;
    return execvp(cmd, args);
}

bool neat_runtime_waitpid(int pid) {
    int wstatus;
    int ret = waitpid(pid, &wstatus, 0);
    if (ret == -1) fprintf(stderr, "waitpid() failed: %s\n", strerror(errno));
    return WIFEXITED(wstatus) && WEXITSTATUS(wstatus) == 0;
}

// No idea why this is necessary.
__attribute__((optnone))
__attribute__((optimize(0)))
bool neat_runtime_symbol_defined_in_main(struct String symbol) {
    // even if a DL is loaded with RTLD_GLOBAL, main symbols are special.
    // so we want to avoid redefining symbols that are in the main program.
    void *main = dlopen(NULL, RTLD_LAZY);
    char *symbolPtr = toStringz(symbol);
    void *sym = dlsym(main, symbolPtr);
    free(symbolPtr);
    dlclose(main);
    return sym ? true : false;
}

void neat_runtime_dlcall(struct String dlfile, struct String fun, void* arg) {
    void *handle = dlopen(toStringz(dlfile), RTLD_LAZY | RTLD_GLOBAL);
    if (!handle) fprintf(stderr, "can't open %.*s - %s\n", (int) dlfile.length, dlfile.ptr, dlerror());
    assert(!!handle);
    void *sym = dlsym(handle, toStringz(fun));
    if (!sym) fprintf(stderr, "can't load symbol '%.*s'\n", (int) fun.length, fun.ptr);
    assert(!!sym);

    ((void(*)(void*)) sym)(arg);
}

void *neat_runtime_alloc(size_t size) {
    return calloc(1, size);
}

extern void _run_unittests();

#ifndef NEAT_NO_MAIN
extern void _main(struct StringArray args);
#endif

int main(int argc, char **argv) {
    struct StringArray args = (struct StringArray) {
        argc,
        malloc(sizeof(struct String) * argc)
    };
    for (int i = 0; i < argc; i++) {
        args.ptr[i] = (struct String) { strlen(argv[i]), argv[i] };
    }
    _run_unittests();
#ifndef NEAT_NO_MAIN
    _main(args);
#endif
    free(args.ptr);
    return 0;
}

//
// fnv hash
//

typedef long long int FNVState;

void *fnv_init()
{
    void *ret = malloc(sizeof(FNVState));
    *(long long int*) ret = 14695981039346656037UL; // offset basis
    return ret;
}

void fnv_add_string(void *state, struct String s)
{
#define HASH (*(long long int*) state)
    for (int i = 0; i < s.length; i++) {
        HASH = HASH ^ s.ptr[i];
        HASH = HASH * 1099511628211;
    }
#undef HASH
}

void fnv_add_long(void *state, long long int value)
{
#define HASH (*(long long int*) state)
    for (int i = 0; i < sizeof(long long int); i++) {
        HASH = HASH ^ (value & 0xff);
        HASH = HASH * 1099511628211;
        value >>= 8;
    }
#undef HASH
}

struct String fnv_hex_value(void *state)
{
    char *ptr = malloc(sizeof(FNVState) * 2 + 1);
    snprintf(ptr, sizeof(FNVState) + 1, "%.*llX", (int) sizeof(FNVState), *(long long int*) state);
    return (struct String) { .length = sizeof(FNVState), .ptr = ptr };
}

//
// polynomial hash
//

#define PRIME 1099511628211

typedef struct {
    long long add, mult;
} PolyHashState;

PolyHashState *poly_init()
{
    // hopefully copied from fnv
    PolyHashState *result = malloc(sizeof(PolyHashState));
    *result = (PolyHashState) {
        .add = 14695981039346656037UL, // offset basis
        .mult = 1,
    };
    return result;
}

void poly_apply_hash(PolyHashState *left, PolyHashState *right)
{
    left->add = left->add * right->mult + right->add;
    left->mult *= right->mult;
}

PolyHashState poly_hash_string(struct String s)
{
    // in a polynomial hash, we apply a string by computing h * p^(s.length) + ((s[0]*p + s[1])*p + s[2])*p...
    // iow, h * p^(s.length) + s[0] * p^(s.length - 1) + s[1] * p^(s.length - 2) + ...
    // p^(s.length) can be efficiently determined by counting along
    PolyHashState result = (PolyHashState) { .add = 0, .mult = 1 };
    // INVERSE index cause we're counting up factors
    for (size_t i = 0; i < s.length; i++) {
        result.add += s.ptr[s.length - 1 - i] * result.mult;
        result.mult *= PRIME;
    }
    return result;
}

void poly_add_string(PolyHashState *state, struct String s)
{
    PolyHashState right = poly_hash_string(s);
    poly_apply_hash(state, &right);
}

PolyHashState poly_hash_long(long long int value)
{
    PolyHashState result = (PolyHashState) { .add = 0, .mult = 1 };
    for (size_t i = 0; i < sizeof(long long int); i++) {
        result.add += ((value >> (8 * i)) & 0xff) * result.mult;
        result.mult *= PRIME;
    }
    return result;
}

void poly_add_long(void *state, long long int value)
{
    PolyHashState right = poly_hash_long(value);
    poly_apply_hash(state, &right);
}

#undef PRIME

struct String poly_hex_value(PolyHashState *state)
{
    struct String ret = string_alloc(sizeof(state->add) * 2 + 1);
    ret.length = snprintf(ret.ptr, ret.length, "%.*llX", (int) sizeof(state->add), state->add);
    return ret;
}

long long int poly_hash_whole_string(struct String s)
{
    PolyHashState state = {
        .add = 14695981039346656037UL, // offset basis
        .mult = 1,
    };
    poly_add_string(&state, s);
    return state.add;
}

// for debug breaks
void debug() { }

void print_backtrace()
{
#ifdef __GLIBC__
    void *array[20];
    int size = backtrace(array, 20);
    char **strings = backtrace_symbols(array, size);
    if (strings != NULL) {
        printf("Backtrace:\n");
        for (int i = 0; i < size; i++)
        printf("  %i: %s\n", i, strings[i]);
    }
    free(strings);
#endif
}

void neat_runtime_refcount_violation(struct String s, ptrdiff_t *ptr)
{
    printf("<%.*s: refcount logic violated: %zd at %p\n", (int) s.length, s.ptr, *ptr, ptr);
    print_backtrace();
}

void neat_runtime_refcount_inc(struct String s, ptrdiff_t *ptr)
{
    // ptrdiff_t result = *ptr += 1;
    ptrdiff_t result = __atomic_add_fetch(ptr, 1, __ATOMIC_ACQ_REL);
    if (result <= 1)
    {
        neat_runtime_refcount_violation(s, ptr);
    }
}

int neat_runtime_refcount_dec(struct String s, ptrdiff_t *ptr)
{
    // ptrdiff_t result = *ptr -= 1;
    ptrdiff_t result = __atomic_sub_fetch(ptr, 1, __ATOMIC_ACQ_REL);
    if (result <= -1)
    {
        neat_runtime_refcount_violation(s, ptr);
    }

    return result == 0;
}

void neat_runtime_class_refcount_inc(void **ptr) {
    if (!ptr) return;
    neat_runtime_refcount_inc((struct String){5, "class", NULL}, (ptrdiff_t*) &ptr[1]);
}

void neat_runtime_class_refcount_dec(void **ptr) {
    if (!ptr) return;
    if (neat_runtime_refcount_dec((struct String){5, "class", NULL}, (ptrdiff_t*) &ptr[1]))
    {
        void (**vtable)(void*) = *(void(***)(void*)) ptr;
        void (*destroy)(void*) = vtable[1];
        destroy(ptr);
        free(ptr);
    }
}

void neat_runtime_refcount_set(size_t *ptr, size_t value)
{
    // *ptr = value;
    __atomic_store(ptr, &value, __ATOMIC_RELEASE);
}

int neat_runtime_errno() { return errno; }

pthread_mutex_t stdout_lock;

__attribute__((constructor)) void neat_runtime_stdout_lock_init(void) {
    pthread_mutex_init(&stdout_lock, NULL);
}

void neat_runtime_lock_stdout(void) {
    pthread_mutex_lock(&stdout_lock);
}

void neat_runtime_unlock_stdout(void) {
    pthread_mutex_unlock(&stdout_lock);
}
