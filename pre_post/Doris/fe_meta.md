```java
// MetaPersistMethod.java
public static MetaPersistMethod create(String name) {
    ...    
}

// InternalCatalog.java
public long saveDb(CountingDataOutputStream dos, long checksum) {
    int dbCount = idToDb.size() - 2;
    checksum ^= dbCount;
    dos.writeInt(dbCount);
    for (Map.Entry<Long, Database> entry : idToDb.entrySet()) {
        Database db = entry.getValue();
        String dbName = db.getFullName();
        // Don't write information_schema & mysql db meta
        if (!InfoSchemaDb.isInfoSchemaDb(dbName) && !MysqlDb.isMysqlDb(dbName)) {
            checksum ^= entry.getKey();
            db.write(dos);
        }
    }
    return checksum;
}

public class Checkpoint {
    protected void runAfterCatalogReady() {
        try {
            doCheckpoint();
        } catch (CheckpointException e) {
            LOG.warn("failed to do checkpoint.", e);
        }
    }

    
}
```