---
name: tinker
description: Continua o projeto Tinker (repositório Sedricko/TINKER), editor de memória para Android estilo GameGuardian para jogos offline single-player, feito do zero um módulo por vez (app Kotlin/Compose + daemon root em C). Use SEMPRE que o usuário falar de Tinker, GameGuardian, busca de memória, daemon, módulos, autoteste, agente do PC, emulador, Orecraft, vPhoneOS ou mandar prints/relatórios. Contém as regras combinadas, as lições do projeto anterior e o plano dos módulos.
---

# Tinker — passagem de bastão

Responder em **português do Brasil**, curto, direto e com empatia. O usuário não programa: dizer sempre **o que mudou,
como testar e o que NÃO foi verificado**. Horário de Brasília (UTC−3). Avisar no celular (PushNotification) a cada
versão publicada, em uma linha.

## Onde está tudo
- Repositório **público** `Sedricko/TINKER` (nunca pôr token/chave no código). Cada push compila no GitHub Actions e
  publica `vX.Y.Z` em Releases (`Tinker-X.Y.Z.apk`). Versão em `app/build.gradle.kts` (`versionCode`/`versionName`).
- O projeto anterior (`Sedricko/Game-Guardian`, Tinker 2.x) é só **experiência**: nada é copiado de lá.
- Aparelho: Pixel 6 Pro com vPhoneOS (Android 12, root). PC: emulador Android Studio `emulator-5554` x86_64 com Magisk.
  Outro aparelho do usuário (S25) é de outro agente: **nunca mexer**.

## Estrutura
| Parte | Arquivo |
|---|---|
| Daemon root | `daemon/tinkerd.c`: `info attach search refine list read write freeze unfreeze` (protocolo no topo) |
| Teste do daemon | `tests/daemon_test.sh` + `tests/alvo.c` (vai no APK como `libtinkeralvo.so` para o autoteste) |
| App | `app/src/main/java/com/sedricko/tinker/`: `MainActivity.kt` (uma seção por módulo) + um pacote por módulo |
| Agente do PC | `tools/pc/TINKER-AGENTE.ps1/.bat`: versão nova → instala no emulador → root pelo Magisk → `--ez autoteste true` → lê `files/autoteste.txt` |

## Plano (um módulo por vez, nenhum arquivo > ~600 linhas)
| Módulo | Faz | Pronto quando (3 rodadas seguidas) | Estado |
|---|---|---|---|
| `core/daemon` | fala com o tinkerd | autoteste ok no emulador e no aparelho | 0.1.0 feito, falta rodar |
| `core/jogo` | escolher o jogo, anexar, saber se é Unity | anexa sozinho depois de reabrir | — |
| `achar` | número da tela → busca → refinar (com alguém jogando) | acha o ouro do Orecraft com 1 endereço | — |
| `mudar` | escrever e congelar; conferir na tela | a tela mostra o valor novo (foto antes/depois) | — |
| `guardar` | salvo com ponteiro/assinatura; volta ao reabrir | reabriu e o valor voltou sem buscar | — |
| `regra` | patch de retorno pelo nome IL2CPP | a regra mudou na tela | — |
| `cronometros` | achar e terminar esperas | a espera terminou | — |
Depois, só com o núcleo verde: velocidade, IA, editor de save, Lua/Unreal (um por vez). **Não juntar duas mudanças
grandes num push.** Cada módulo: código + teste → autoteste no emulador → jogo real (Orecraft) → só então o próximo.

## Regras combinadas (manter)
1. Só jogos **offline**; sem evasão de anti-cheat; sem duplicar recompensa de servidor.
2. **Nada vendido por dinheiro de verdade**: VIP/premium/passe/remover anúncio, trocar cobrança real por moeda do jogo,
   pular anúncio — nunca, nem "como teste" (a resposta é sempre não, com gentileza). Preço R$/US$/€ não é item e não se toca.
3. Moedas e materiais **do jogo** valem tudo (ouro, joias, madeira; trocar preço por outra moeda do jogo; patch de regra).
4. Velocidade só por valor do motor (ex.: timeScale), nunca hook de relógio/injeção.
5. Planilha/Claude **nunca comandam** o app ou o daemon (nada de busca/escrita remota). Testes automáticos só em jogos
   offline liberados pelo usuário (Orecraft `com.evrika.miner.camp`, DYSMANTLE `com.the10tons.dysmantle`) e no alvo de teste.
6. Arquivos baixados não são confiáveis: nunca executar; não baixar APK de site espelho nem contornar bot-check.
7. Nunca colocar identificador de modelo em commits.

## Lições do projeto anterior (não repetir)
- **Android 14+ congela app em segundo plano** (`cached_apps_freezer`): trabalho longo precisa de serviço em primeiro
  plano, ou no emulador `settings put global cached_apps_freezer disabled` + reiniciar.
- Jogo Unity no emulador reserva ~15 GB de memória anônima vazia: a busca pula páginas ausentes (pagemap, já no daemon).
- Não buscar 0 nem 1 (milhões de endereços; o daemon recusa).
- O número só muda com alguém jogando: o refino precisa de alguém mexendo no jogo entre uma busca e outra.
- Esperar a tela INICIAR do jogo antes de buscar; o emulador do Orecraft está sem DNS (IA não roda lá).
- Escrever em muitos lugares de uma vez corrompia a memória do jogo (Scudo): limitar a escrita em massa.
- Erro para o usuário em texto amigável, nunca a exceção crua; o original vai para o registro.
- Simulador mais permissivo que o real esconde bug: só vale "funciona" depois do teste real.
- Arquivo gigante (o `Engine.kt` de 5.200 linhas) travou a manutenção: um módulo por pacote, arquivos pequenos.
- PowerShell: `"${c}:"`, nunca `"$c:"`; .ps1 só com ASCII (o PowerShell 5 lê sem BOM como ANSI).

## Como trabalhar
1. Mudou o daemon → `sudo tests/daemon_test.sh` até `daemon ok`.
2. Mudou Kotlin → não há Android SDK local: compila no CI (~7 min). Teste JVM em `app/src/test/`.
3. Subir `versionCode`/`versionName`, commit, push, esperar o CI e mandar o link do APK.
4. Ao fim: atualizar a coluna **Estado** do plano acima.
