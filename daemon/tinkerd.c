// tinkerd: daemon root do Tinker. Lê e escreve a memória de um processo (jogo offline).
// Protocolo de texto: um comando por linha em stdin; a resposta são linhas de dados e no fim "ok [status]" ou "err motivo".
//   info                          -> ok pid=N uid=N
//   attach PID                    -> ok
//   search TIPO VALOR             -> ok N        (tipos: i32 f32 i64 f64; guarda os endereços achados)
//   refine VALOR                  -> ok N        (fica só com os que valem VALOR agora)
//   list N                        -> "endereço tipo valor" por linha, ok TOTAL
//   read END TIPO                 -> ok valor
//   write END TIPO VALOR          -> ok
//   freeze END TIPO VALOR         -> ok          (reescreve a cada 100 ms)
//   unfreeze END|all              -> ok
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <math.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/uio.h>
#include <unistd.h>

// o autoteste do app se anexa ao próprio daemon e procura este valor (sem tocar em jogo nenhum)
volatile int32_t tinker_selftest_value = 271828182;

enum { I32, F32, I64, F64, NTYPES };
static const char *type_names[NTYPES] = {"i32", "f32", "i64", "f64"};
static const int type_size[NTYPES] = {4, 4, 8, 8};

static pid_t target;
static int mem_fd = -1;
static int hit_type;
static uint64_t *hits;
static size_t nhits, cap;

#define MAX_FROZEN 64
static struct { uint64_t addr; int type; uint8_t bytes[8]; } frozen[MAX_FROZEN];
static int nfrozen;
static pthread_mutex_t frozen_lock = PTHREAD_MUTEX_INITIALIZER;

static int type_of(const char *s) {
    for (int t = 0; t < NTYPES; t++) if (!strcmp(s, type_names[t])) return t;
    return -1;
}

// process_vm_* é o caminho rápido; alguns kernels/SELinux recusam e /proc/pid/mem resolve
static ssize_t mem_read(uint64_t addr, void *buf, size_t len) {
    struct iovec l = {buf, len}, r = {(void *)(uintptr_t)addr, len};
    ssize_t n = process_vm_readv(target, &l, 1, &r, 1, 0);
    if (n < 0 && mem_fd >= 0) n = pread64(mem_fd, buf, len, (off64_t)addr);
    return n;
}

static ssize_t mem_write(uint64_t addr, const void *buf, size_t len) {
    struct iovec l = {(void *)buf, len}, r = {(void *)(uintptr_t)addr, len};
    ssize_t n = process_vm_writev(target, &l, 1, &r, 1, 0);
    if (n < 0 && mem_fd >= 0) n = pwrite64(mem_fd, buf, len, (off64_t)addr);
    return n;
}

// texto -> bytes do tipo; 0 se o texto não é número
static int encode(int t, const char *s, uint8_t *out) {
    char *end;
    errno = 0;
    if (t == I32 || t == I64) {
        long long v = strtoll(s, &end, 0);
        if (end == s || *end || errno) return 0;
        if (t == I32) { int32_t x = (int32_t)v; memcpy(out, &x, 4); } else { int64_t x = v; memcpy(out, &x, 8); }
    } else {
        double v = strtod(s, &end);
        if (end == s || *end || errno) return 0;
        if (t == F32) { float x = (float)v; memcpy(out, &x, 4); } else memcpy(out, &v, 8);
    }
    return 1;
}

static void format(int t, const uint8_t *b, char *out, size_t n) {
    if (t == I32) { int32_t x; memcpy(&x, b, 4); snprintf(out, n, "%" PRId32, x); }
    else if (t == I64) { int64_t x; memcpy(&x, b, 8); snprintf(out, n, "%" PRId64, x); }
    else if (t == F32) { float x; memcpy(&x, b, 4); snprintf(out, n, "%g", x); }
    else { double x; memcpy(&x, b, 8); snprintf(out, n, "%g", x); }
}

// número da tela é arredondado: float vale se estiver a menos de 0,001 (ou 0,01% para números grandes)
static int matches(int t, const uint8_t *mem, const uint8_t *want) {
    if (t == I32 || t == I64) return !memcmp(mem, want, type_size[t]);
    double a, b;
    if (t == F32) { float x, y; memcpy(&x, mem, 4); memcpy(&y, want, 4); a = x; b = y; }
    else { memcpy(&a, mem, 8); memcpy(&b, want, 8); }
    return fabs(a - b) <= fmax(1e-3, fabs(b) * 1e-4);
}

static void add_hit(uint64_t a) {
    if (nhits == cap) {
        cap = cap ? cap * 2 : 4096;
        uint64_t *n = realloc(hits, cap * sizeof *hits);
        if (!n) { perror("realloc"); exit(1); }
        hits = n;
    }
    hits[nhits++] = a;
}

// memória anônima que o jogo reservou e nunca usou não tem página: pular sem ler.
// No emulador um jogo Unity reservava ~15 GB assim e a busca levava minutos em vez de segundos.
static int chunk_unused(int pagemap, uint64_t addr, size_t len) {
    if (pagemap < 0) return 0;
    long ps = sysconf(_SC_PAGESIZE);
    size_t np = len / ps;
    uint64_t e[256];
    for (size_t i = 0; i < np; i += 256) {
        size_t k = np - i < 256 ? np - i : 256;
        if (pread64(pagemap, e, k * 8, (off64_t)((addr / ps + i) * 8)) != (ssize_t)(k * 8)) return 0;
        for (size_t j = 0; j < k; j++) if (e[j] >> 62) return 0;  // bit 63 presente, bit 62 no swap
    }
    return 1;
}

static void cmd_search(int t, const uint8_t *want) {
    char path[64], line[512];
    snprintf(path, sizeof path, "/proc/%d/maps", target);
    FILE *maps = fopen(path, "r");
    if (!maps) { printf("err não abri %s: %s\n", path, strerror(errno)); return; }
    snprintf(path, sizeof path, "/proc/%d/pagemap", target);
    int pagemap = open(path, O_RDONLY);
    nhits = 0;
    hit_type = t;
    const size_t chunk = 1 << 20;
    uint8_t *buf = malloc(chunk);
    int sz = type_size[t];
    while (fgets(line, sizeof line, maps)) {
        uint64_t lo, hi;
        char perms[5] = "", file[400] = "";
        if (sscanf(line, "%" SCNx64 "-%" SCNx64 " %4s %*s %*s %*s %399[^\n]", &lo, &hi, perms, file) < 3) continue;
        // só o que o jogo pode escrever (rw); a memória do sistema gráfico e de dispositivos não guarda valores do jogo
        if (perms[0] != 'r' || perms[1] != 'w') continue;
        if (strstr(file, "/dev/") || strstr(file, "kgsl") || strstr(file, "mali")) continue;
        int anon = !file[0] || file[0] == '[';
        for (uint64_t a = lo; a < hi; a += chunk) {
            size_t len = hi - a < chunk ? hi - a : chunk;
            if (anon && chunk_unused(pagemap, a, len)) continue;
            ssize_t n = mem_read(a, buf, len);
            if (n <= 0) continue;
            for (ssize_t off = 0; off + sz <= n; off += 4)
                if (matches(t, buf + off, want)) add_hit(a + off);
        }
    }
    free(buf);
    if (pagemap >= 0) close(pagemap);
    fclose(maps);
    printf("ok %zu\n", nhits);
}

static void cmd_refine(const uint8_t *want) {
    size_t keep = 0;
    uint8_t b[8];
    for (size_t i = 0; i < nhits; i++)
        if (mem_read(hits[i], b, type_size[hit_type]) == type_size[hit_type] && matches(hit_type, b, want)) hits[keep++] = hits[i];
    nhits = keep;
    printf("ok %zu\n", nhits);
}

static void *freezer(void *arg) {
    (void)arg;
    for (;;) {
        pthread_mutex_lock(&frozen_lock);
        for (int i = 0; i < nfrozen; i++) mem_write(frozen[i].addr, frozen[i].bytes, type_size[frozen[i].type]);
        pthread_mutex_unlock(&frozen_lock);
        usleep(100000);
    }
    return NULL;
}

static int need_target(void) {
    if (!target) { printf("err nenhum jogo anexado\n"); return 0; }
    if (kill(target, 0) && errno == ESRCH) { printf("err o jogo fechou\n"); return 0; }
    return 1;
}

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    pthread_t th;
    pthread_create(&th, NULL, freezer, NULL);
    printf("ok tinkerd 1\n");
    char line[1024];
    while (fgets(line, sizeof line, stdin)) {
        line[strcspn(line, "\r\n")] = 0;
        char c[32] = "", a1[256] = "", a2[64] = "", a3[256] = "";
        int n = sscanf(line, "%31s %255s %63s %255s", c, a1, a2, a3);
        uint8_t v[8];
        int t;
        if (n < 1) continue;
        if (!strcmp(c, "info")) printf("ok pid=%d uid=%d\n", getpid(), getuid());
        else if (!strcmp(c, "attach") && n == 2) {
            pid_t p = atoi(a1);
            if (p <= 0 || kill(p, 0)) { printf("err processo %s não existe\n", a1); continue; }
            pthread_mutex_lock(&frozen_lock);
            nfrozen = 0;
            if (mem_fd >= 0) close(mem_fd);
            char path[64];
            snprintf(path, sizeof path, "/proc/%d/mem", p);
            mem_fd = open(path, O_RDWR);
            target = p;
            nhits = 0;
            pthread_mutex_unlock(&frozen_lock);
            printf("ok\n");
        }
        else if (!strcmp(c, "search") && n == 3) {
            if ((t = type_of(a1)) < 0) { printf("err tipo %s não existe (use i32 f32 i64 f64)\n", a1); continue; }
            if (!encode(t, a2, v)) { printf("err %s não é número\n", a2); continue; }
            // 0 e 1 estão em milhões de lugares: a lista fica enorme e o refino não termina
            if (!strcmp(a2, "0") || !strcmp(a2, "1")) { printf("err 0 e 1 aparecem em milhões de lugares: busque outro número\n"); continue; }
            if (need_target()) cmd_search(t, v);
        }
        else if (!strcmp(c, "refine") && n == 2) {
            if (!nhits) { printf("err nada para refinar: faça uma busca antes\n"); continue; }
            if (!encode(hit_type, a1, v)) { printf("err %s não é número\n", a1); continue; }
            if (need_target()) cmd_refine(v);
        }
        else if (!strcmp(c, "list") && n == 2) {
            size_t max = strtoul(a1, NULL, 10);
            uint8_t b[8];
            char s[64];
            for (size_t i = 0; i < nhits && i < max; i++) {
                if (mem_read(hits[i], b, type_size[hit_type]) != type_size[hit_type]) continue;
                format(hit_type, b, s, sizeof s);
                printf("%" PRIx64 " %s %s\n", hits[i], type_names[hit_type], s);
            }
            printf("ok %zu\n", nhits);
        }
        else if (!strcmp(c, "read") && n == 3) {
            if ((t = type_of(a2)) < 0) { printf("err tipo %s não existe\n", a2); continue; }
            if (!need_target()) continue;
            char s[64];
            if (mem_read(strtoull(a1, NULL, 16), v, type_size[t]) != type_size[t]) { printf("err não li %s\n", a1); continue; }
            format(t, v, s, sizeof s);
            printf("ok %s\n", s);
        }
        else if ((!strcmp(c, "write") || !strcmp(c, "freeze")) && n == 4) {
            if ((t = type_of(a2)) < 0) { printf("err tipo %s não existe\n", a2); continue; }
            if (!encode(t, a3, v)) { printf("err %s não é número\n", a3); continue; }
            if (!need_target()) continue;
            uint64_t addr = strtoull(a1, NULL, 16);
            if (mem_write(addr, v, type_size[t]) != type_size[t]) { printf("err não escrevi em %s\n", a1); continue; }
            if (c[0] == 'f') {
                pthread_mutex_lock(&frozen_lock);
                int i = 0;
                while (i < nfrozen && frozen[i].addr != addr) i++;
                if (i == MAX_FROZEN) { pthread_mutex_unlock(&frozen_lock); printf("err já tem %d valores congelados\n", MAX_FROZEN); continue; }
                frozen[i].addr = addr;
                frozen[i].type = t;
                memcpy(frozen[i].bytes, v, 8);
                if (i == nfrozen) nfrozen++;
                pthread_mutex_unlock(&frozen_lock);
            }
            printf("ok\n");
        }
        else if (!strcmp(c, "unfreeze") && n == 2) {
            pthread_mutex_lock(&frozen_lock);
            if (!strcmp(a1, "all")) nfrozen = 0;
            else {
                uint64_t addr = strtoull(a1, NULL, 16);
                for (int i = 0; i < nfrozen; i++) if (frozen[i].addr == addr) frozen[i--] = frozen[--nfrozen];
            }
            pthread_mutex_unlock(&frozen_lock);
            printf("ok\n");
        }
        else printf("err comando desconhecido: %s\n", line);
    }
    return 0;
}
