package com.sedricko.tinker.core.daemon

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import java.io.StringWriter

class DaemonTest {
    private fun daemon(replies: String, sent: StringWriter = StringWriter()) = Daemon(sent, replies.reader().buffered())
    private fun erro(block: () -> Unit) = runCatching(block).exceptionOrNull()?.message

    @Test fun linhasAntesDoOk() {
        val sent = StringWriter()
        val r = daemon("ok tinkerd 1\n10 i32 5\n20 i32 5\nok 2\n", sent).call("list 5")
        assertEquals(listOf("10 i32 5", "20 i32 5"), r.lines)
        assertEquals(2L, r.count)
        assertEquals("list 5\n", sent.toString())
    }

    @Test fun erroViraExcecaoComOMotivo() =
        assertEquals("o jogo fechou", erro { daemon("ok tinkerd 1\nerr o jogo fechou\n").call("read 10 i32") })

    @Test fun semRootOuDaemonErrado() {
        assertEquals("Sem root: o su recusou ou não existe.", erro { daemon("") })
        assertTrue(erro { daemon("Permission denied\n") }!!.startsWith("O daemon não respondeu"))
    }

    @Test fun daemonFechouNoMeio() = assertEquals("O daemon fechou.", erro { daemon("ok tinkerd 1\nlinha\n").call("list 5") })
}
