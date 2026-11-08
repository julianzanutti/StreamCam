package com.streamcam.diagnostics

import kotlin.math.floor
import kotlin.math.roundToInt

/** Converts interleaved PCM16 input blocks to the fixed 48 kHz USB wire rate. */
internal object PcmResampler {
    const val OUTPUT_RATE = 48_000

    fun to48kMono(input: ByteArray, byteCount: Int, inputRate: Int): ByteArray {
        require(inputRate in 8_000..48_000 && byteCount in 2..input.size && byteCount % 2 == 0)
        val inputFrames=byteCount/2
        val outputFrames=((inputFrames.toLong()*OUTPUT_RATE+inputRate-1)/inputRate).toInt()
        require(outputFrames in 1..960) { "Un bloque de captura supera los 20 ms." }
        if(inputRate==OUTPUT_RATE)return input.copyOf(byteCount)

        val result=ByteArray(outputFrames*2)
        for(outputIndex in 0 until outputFrames) {
            val sourcePosition=outputIndex.toDouble()*inputRate/OUTPUT_RATE
            val firstIndex=floor(sourcePosition).toInt().coerceAtMost(inputFrames-1)
            val secondIndex=(firstIndex+1).coerceAtMost(inputFrames-1)
            val fraction=(sourcePosition-firstIndex).toFloat()
            val first=sampleAt(input,firstIndex)
            val second=sampleAt(input,secondIndex)
            val sample=(first+(second-first)*fraction).roundToInt().coerceIn(Short.MIN_VALUE.toInt(),Short.MAX_VALUE.toInt())
            result[outputIndex*2]=(sample and 0xff).toByte()
            result[outputIndex*2+1]=((sample ushr 8) and 0xff).toByte()
        }
        return result
    }

    private fun sampleAt(data:ByteArray,index:Int):Int {
        val offset=index*2
        return ((data[offset].toInt() and 0xff) or (data[offset+1].toInt() shl 8)).toShort().toInt()
    }
}

/** Assembles short AudioRecord reads into fixed 20 ms PCM16 packets without losing byte alignment. */
internal class Pcm16BlockAssembler(blockFrames: Int) {
    private val block=ByteArray(blockFrames*2)
    private var filled=0
    val remainingBytes:Int get()=block.size-filled

    init { require(blockFrames in 1..960) }

    fun append(input:ByteArray,offset:Int,byteCount:Int):ByteArray? {
        require(offset>=0 && byteCount>=0 && offset<=input.size-byteCount)
        require(byteCount<=remainingBytes) { "AudioRecord devolvió más bytes que el espacio disponible." }
        input.copyInto(block,filled,offset,offset+byteCount)
        filled+=byteCount
        if(filled!=block.size)return null
        filled=0
        return block.copyOf()
    }
}
