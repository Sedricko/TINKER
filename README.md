# Tinker

Editor de memória para Android, para jogos **offline single-player**. Feito do zero, **um módulo por vez**:
nada novo entra antes de o anterior passar no teste automático e no jogo real.

> Só jogos offline. Nada vendido por dinheiro de verdade é tocado (VIP, passe, remover anúncio, preço em R$/US$).

## Requisitos
Android 8+, arm64 (ou emulador x86_64), **root** (Magisk/KernelSU ou máquina virtual com root, como o vPhoneOS).

## Baixar
Releases → `Tinker-X.Y.Z.apk` da versão mais recente.

## Estrutura
| Pasta | O que é |
|---|---|
| `daemon/tinkerd.c` | Daemon root em C (sem dependências): lê e escreve a memória do jogo. Protocolo de texto no topo do arquivo |
| `app/` | App Kotlin + Compose. Um pacote por módulo em `com.sedricko.tinker` (hoje: `core/daemon`) |
| `tests/` | `daemon_test.sh` testa o daemon no Linux contra `alvo.c` (um "jogo" com valores conhecidos) |
| `tools/pc/` | Agente do PC: instala cada versão nova no emulador e roda o autoteste |
| `.claude/skills/tinker/` | Passagem de bastão: regras, lições aprendidas e o plano dos módulos |

## Testar
- Daemon: `sudo tests/daemon_test.sh` (termina com `daemon ok`).
- App: o GitHub Actions roda os testes, compila e publica o APK a cada push.
- No aparelho: abrir o Tinker → **Rodar autoteste**.
