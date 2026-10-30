#!/usr/bin/env bash
# Testa o tinkerd no Linux contra um processo de verdade (tests/alvo.c). Precisa de root (sudo) como no Android.
set -euo pipefail
cd "$(dirname "$0")/.."
out=$(mktemp -d)
trap 'kill $alvo_pid $d_pid 2>/dev/null || true; rm -rf "$out"' EXIT
gcc -O2 -Wall -Werror -pthread -o "$out/tinkerd" daemon/tinkerd.c -lm
gcc -O2 -o "$out/alvo" tests/alvo.c

mkfifo "$out/in" "$out/res"
"$out/alvo" > "$out/alvo.txt" &
alvo_pid=$!
for _ in $(seq 50); do [ -s "$out/alvo.txt" ] && break; sleep 0.1; done
read -r pid ouro vida xp < "$out/alvo.txt"
"$out/tinkerd" < "$out/in" > "$out/res" &
d_pid=$!
exec 3> "$out/in" 4< "$out/res"

# manda um comando e devolve a resposta inteira (linhas de dados + ok/err)
call() {
    echo "$1" >&3
    local r="" l
    while IFS= read -r -t 30 l <&4; do
        r+="$l"$'\n'
        case "$l" in ok|ok\ *|err\ *) break ;; esac
    done
    printf '%s' "$r"
}
falhas=0
check() {  # nome, comando, texto esperado na resposta
    local r; r=$(call "$2")
    if [[ "$r" == *"$3"* ]]; then echo "ok   $1"; else echo "FALHA $1: '$2' -> $r (esperava '$3')"; falhas=$((falhas + 1)); fi
}

read -r -t 5 hello <&4; [[ "$hello" == "ok tinkerd"* ]] || { echo "FALHA hello: $hello"; exit 1; }
check "info com uid" "info" "uid=0"
check "sem jogo" "search i32 5" "err nenhum jogo"
check "anexa" "attach $pid" "ok"
t0=$(date +%s%N)
check "busca Dword (1 endereço)" "search i32 123456789" "ok 1"
ms=$(( ($(date +%s%N) - t0) / 1000000 ))
echo "     busca levou $ms ms (pulando a reserva vazia de 512 MB)"
[ "$ms" -lt 5000 ] || { echo "FALHA busca lenta: $ms ms"; falhas=$((falhas + 1)); }
check "lista mostra o endereço" "list 5" "$ouro i32 123456789"
check "escreve" "write $ouro i32 4999" "ok"
check "lê o novo" "read $ouro i32" "ok 4999"
check "refina pelo novo" "refine 4999" "ok 1"
check "refina por outro some" "refine 5000" "ok 0"
check "float arredondado da tela" "search f32 87.5" "ok"
check "float no endereço certo" "list 50" "$vida f32 87.5"
check "qword" "search i64 9876543210" "ok 1"
check "congela" "freeze $ouro i32 777" "ok"
call "write $ouro i32 1" > /dev/null; sleep 0.4
check "congelado segura" "read $ouro i32" "ok 777"
check "descongela" "unfreeze all" "ok"
call "write $ouro i32 1" > /dev/null; sleep 0.4
check "descongelado muda" "read $ouro i32" "ok 1"
check "recusa 0" "search i32 0" "err 0 e 1"
check "tipo errado" "search abc 5" "err tipo"
check "não número" "write $ouro i32 xyz" "err xyz"
check "comando desconhecido" "voar" "err comando"
# o autoteste do app: o daemon se anexa a ele mesmo e acha o valor de teste
check "anexa a si mesmo" "attach $d_pid" "ok"
check "acha o valor de teste" "search i32 271828182" "ok"
check "anexa ao alvo de novo" "attach $pid" "ok"
kill "$alvo_pid"; wait "$alvo_pid" 2>/dev/null || true
check "avisa que o jogo fechou" "read $ouro i32" "err o jogo fechou"

[ "$falhas" -eq 0 ] && echo "daemon ok" || { echo "$falhas falha(s)"; exit 1; }
