package com.sedricko.tinker.core.daemon

import java.io.File
import kotlin.concurrent.thread

/**
 * Autoteste do módulo core/daemon: root, o ciclo de memória no próprio daemon e o alvo de teste (tests/alvo.c).
 * Não toca em jogo nenhum. 1ª linha = resumo (o agente do PC usa como título no relatório); falhas começam com "FALHA".
 */
object Autoteste {
    fun run(libDir: String, version: String): String {
        val lib = "$libDir/libtinkerd.so"
        val out = mutableListOf<String>()
        fun check(name: String, work: () -> String) {
            val t0 = System.currentTimeMillis()
            val r = runCatching(work)
            out += (if (r.isSuccess) "OK" else "FALHA") + " $name: " +
                (r.getOrNull() ?: r.exceptionOrNull()?.let { it.message ?: it.toString() }) + " (${System.currentTimeMillis() - t0} ms)"
        }
        fun <T> withDaemon(block: (Daemon) -> T): T = Daemon.start(lib).let { d -> try { block(d) } finally { d.close() } }

        check("daemon e root") { withDaemon { d -> d.call("info").status.also { require("uid=0" in it) { "sem root: $it" } } } }
        check("memória no próprio daemon: busca, escrita, refino e congelamento") {
            withDaemon { d ->
                d.call("attach " + d.call("info").status.substringAfter("pid=").substringBefore(' '))
                require(d.call("search i32 271828182").count >= 1) { "a busca não achou o valor de teste" }
                val addr = d.call("list 1").lines.firstOrNull()?.substringBefore(' ') ?: error("lista vazia")
                d.call("write $addr i32 314159")
                require(d.call("read $addr i32").status == "314159") { "a escrita não gravou" }
                require(d.call("refine 314159").count >= 1) { "o refino perdeu o valor" }
                d.call("freeze $addr i32 777")
                d.call("write $addr i32 5")
                Thread.sleep(400)
                require(d.call("read $addr i32").status == "777") { "o congelamento não segurou" }
                d.call("unfreeze all")
                "ok"
            }
        }
        val alvo = "$libDir/libtinkeralvo.so"
        if (File(alvo).exists()) check("alvo de teste: anexar e achar o ouro com 1 endereço") { onAlvo(alvo, ::withDaemon) }
        else out += "FALHA alvo de teste: libtinkeralvo.so não veio no APK"

        return "Tinker $version, núcleo daemon: ${out.count { it.startsWith("OK") }} de ${out.size} ok\n" + out.joinToString("\n")
    }

    /** Abre o tests/alvo.c como root; a 1ª linha dele é "pid ouro vida xp" (endereços em hexadecimal). */
    private fun onAlvo(alvo: String, withDaemon: ((Daemon) -> String) -> String): String {
        val proc = ProcessBuilder("su", "-c", "exec $alvo").start()
        var info: List<String> = emptyList()
        thread { info = proc.inputStream.bufferedReader().readLine()?.trim()?.split(' ').orEmpty() }.join(5000)
        val pid = info.firstOrNull().orEmpty()
        try {
            require(info.size == 4) { "o alvo de teste não abriu" }
            val ouro = info[1]
            return withDaemon { d ->
                d.call("attach $pid")
                require(d.call("search i32 123456789").count == 1L) { "não achou o ouro com 1 endereço" }
                d.call("write $ouro i32 4999")
                require(d.call("refine 4999").count == 1L) { "o refino não ficou com 1 endereço" }
                "ok"
            }
        } finally {
            if (pid.isNotEmpty()) runCatching { ProcessBuilder("su", "-c", "kill -9 $pid").start().waitFor() }
            proc.destroy()
        }
    }
}
