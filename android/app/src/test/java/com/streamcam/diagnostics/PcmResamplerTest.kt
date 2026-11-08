package com.streamcam.diagnostics

import org.junit.Assert.assertEquals
import org.junit.Assert.assertThrows
import org.junit.Test

class PcmResamplerTest {
    @Test fun native48kBlockIsPreserved() {
        val input=pcm16(960,1234)
        assertEquals(input.toList(),PcmResampler.to48kMono(input,input.size,48_000).toList())
    }

    @Test fun commonBluetoothAndUsbRatesProduce20ms48kBlocks() {
        for(rate in listOf(44_100,32_000,24_000,16_000,12_000,8_000)) {
            val input=pcm16(rate/50,1234)
            val output=PcmResampler.to48kMono(input,input.size,rate)
            assertEquals("rate $rate",960,output.size/2)
            for(index in output.indices step 2) {
                val sample=((output[index].toInt() and 0xff) or (output[index+1].toInt() shl 8)).toShort().toInt()
                assertEquals("rate $rate",1234,sample)
            }
        }
    }

    @Test fun partialReadsKeepTheirDuration() {
        val output=PcmResampler.to48kMono(pcm16(441,500),882,44_100)
        assertEquals(480,output.size/2)
    }

    @Test fun rejectsInvalidAndOversizedBlocks() {
        assertThrows(IllegalArgumentException::class.java) { PcmResampler.to48kMono(byteArrayOf(0),1,48_000) }
        assertThrows(IllegalArgumentException::class.java) { PcmResampler.to48kMono(pcm16(961,0),1922,48_000) }
    }

    @Test fun shortAudioRecordReadsAssembleOneExactTwentyMillisecondPacket() {
        val assembler=Pcm16BlockAssembler(882)
        val first=pcm16(317,100)
        val second=pcm16(565,200)
        assertEquals(null,assembler.append(first,0,first.size))
        assertEquals(1130,assembler.remainingBytes)
        val complete=assembler.append(second,0,second.size)
        assertEquals(1764,complete?.size)
        assertEquals(1764,assembler.remainingBytes)
        assertEquals(960,PcmResampler.to48kMono(complete!!,complete.size,44_100).size/2)
    }

    @Test fun assemblerCarriesOddReadBoundariesWithoutDroppingPcmBytes() {
        val assembler=Pcm16BlockAssembler(2)
        val first=byteArrayOf(1,2,3)
        assertEquals(null,assembler.append(first,0,first.size))
        val second=byteArrayOf(4)
        val complete=assembler.append(second,0,second.size)
        assertEquals(listOf<Byte>(1,2,3,4),complete?.toList())
    }

    private fun pcm16(frames:Int,value:Int):ByteArray = ByteArray(frames*2).also { bytes->
        for(index in 0 until frames) {
            bytes[index*2]=(value and 0xff).toByte()
            bytes[index*2+1]=((value ushr 8) and 0xff).toByte()
        }
    }
}
