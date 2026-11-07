package com.streamcam.diagnostics

import java.net.Inet4Address
import java.net.InetAddress
import java.net.InetSocketAddress
import java.net.NetworkInterface
import java.net.Socket
import javax.net.SocketFactory

/** Bind signaling to the physical local subnet selected by the numeric PC endpoint. */
internal class LocalSocketFactory(pcHost: String) : SocketFactory() {
    private val remote = InetAddress.getByName(pcHost) as Inet4Address
    private val local: InetAddress
    val localAddress: String get() = local.hostAddress!!
    init {
        val matches = if(remote.hostAddress=="127.0.0.1") listOf(remote) else NetworkInterface.getNetworkInterfaces().toList().filter {
            it.isUp && !it.isLoopback && it.name.matches(Regex("(wlan|swlan|rndis|usb|eth|ncm|ap|bridge)[A-Za-z0-9_.-]*"))
        }.flatMap { network -> network.interfaceAddresses.filter { entry ->
            val address=entry.address
            if(address !is Inet4Address || entry.networkPrefixLength !in 1..30) false
            else {
                val prefix=entry.networkPrefixLength.toInt()
                val mask=-1 shl (32-prefix)
                fun bits(bytes: ByteArray)=bytes.fold(0) { acc, byte -> (acc shl 8) or (byte.toInt() and 255) }
                (bits(address.address) and mask)==(bits(remote.address) and mask)
            }
        }.map { it.address } }
        require(matches.size==1) { "No hay una única interfaz local hacia la PC. Revisá Wi-Fi o red USB." }
        local=matches.single()
    }
    override fun createSocket(): Socket = object : Socket() {
        override fun connect(endpoint: java.net.SocketAddress, timeout: Int) {
            android.util.Log.d("StreamCamPairing","TCP connect begin")
            try { super.connect(endpoint,timeout); android.util.Log.d("StreamCamPairing","TCP connected") }
            catch(error: Exception) { android.util.Log.e("StreamCamPairing","TCP failed: ${error.javaClass.simpleName}"); throw error }
        }
    }.apply { bind(InetSocketAddress(local,0)) }
    override fun createSocket(host: String, port: Int): Socket = createSocket().apply { connect(InetSocketAddress(host,port),10000) }
    override fun createSocket(host: InetAddress, port: Int): Socket = createSocket(host.hostAddress!!,port)
    override fun createSocket(host: String, port: Int, localHost: InetAddress, localPort: Int): Socket = createSocket(host,port)
    override fun createSocket(host: InetAddress, port: Int, localHost: InetAddress, localPort: Int): Socket = createSocket(host,port)
}
