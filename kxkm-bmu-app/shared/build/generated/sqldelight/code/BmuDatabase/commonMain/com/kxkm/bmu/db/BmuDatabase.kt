package com.kxkm.bmu.db

import app.cash.sqldelight.Transacter
import app.cash.sqldelight.db.QueryResult
import app.cash.sqldelight.db.SqlDriver
import app.cash.sqldelight.db.SqlSchema
import com.kxkm.bmu.db.shared.newInstance
import com.kxkm.bmu.db.shared.schema
import kotlin.Unit

public interface BmuDatabase : Transacter {
  public val bmuDatabaseQueries: BmuDatabaseQueries

  public companion object {
    public val Schema: SqlSchema<QueryResult.Value<Unit>>
      get() = BmuDatabase::class.schema

    public operator fun invoke(driver: SqlDriver): BmuDatabase =
        BmuDatabase::class.newInstance(driver)
  }
}
