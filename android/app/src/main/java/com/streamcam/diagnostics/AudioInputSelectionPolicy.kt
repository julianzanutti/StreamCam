package com.streamcam.diagnostics

internal data class AudioInputSelection(val index:Int,val key:String,val missingSavedInput:Boolean)

/** Resolves runtime-scoped Android input IDs without silently selecting an ambiguous route. */
internal object AudioInputSelectionPolicy {
    fun resolve(savedKey:String?,availableKeys:List<String>,automaticKey:String):AudioInputSelection {
        val requested=savedKey?.takeIf { it.isNotBlank() } ?: automaticKey
        val matches=availableKeys.withIndex().filter { it.value==requested }
        if(matches.size==1)return AudioInputSelection(matches.single().index,requested,false)

        val automatic=availableKeys.withIndex().singleOrNull { it.value==automaticKey }
            ?: throw IllegalArgumentException("Automatic audio input must be present exactly once.")
        return AudioInputSelection(automatic.index,automaticKey,requested!=automaticKey)
    }
}
