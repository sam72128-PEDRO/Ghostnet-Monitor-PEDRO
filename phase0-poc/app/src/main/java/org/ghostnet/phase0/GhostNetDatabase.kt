package org.ghostnet.phase0

import android.content.Context
import android.database.sqlite.SQLiteDatabase
import android.database.sqlite.SQLiteOpenHelper

class GhostNetDatabase(context: Context) : SQLiteOpenHelper(context, "ghostnet.db", null, 1) {
    override fun onCreate(db: SQLiteDatabase) {
        db.execSQL("CREATE TABLE traffic (id INTEGER PRIMARY KEY AUTOINCREMENT, utc INTEGER NOT NULL, source TEXT NOT NULL, destination TEXT NOT NULL, message TEXT NOT NULL, frequency_hz INTEGER NOT NULL, snr REAL, audio_offset_hz REAL, decoder_status TEXT NOT NULL)")
    }

    override fun onUpgrade(db: SQLiteDatabase, oldVersion: Int, newVersion: Int) = Unit
}
