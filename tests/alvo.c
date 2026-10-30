// Processo alvo do teste do daemon: guarda valores conhecidos e espera, como um jogo parado numa tela.
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main(void) {
    volatile int32_t *ouro = malloc(64);
    volatile float *vida = malloc(64);
    volatile int64_t *xp = malloc(64);
    *ouro = 123456789;
    *vida = 87.5f;
    *xp = 9876543210LL;
    // reserva grande e nunca usada (como a do Unity no emulador): a busca tem de pular sem ler
    volatile char *vazio = malloc(512u << 20);
    (void)vazio;
    printf("%d %lx %lx %lx\n", getpid(), (unsigned long)ouro, (unsigned long)vida, (unsigned long)xp);
    fflush(stdout);
    pause();
    return 0;
}
