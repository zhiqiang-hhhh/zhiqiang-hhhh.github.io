# Start Up  
<center>
<img alt="picture 1" src="../../images/b54750afebc9e83dd903f9da36c39d1138725a13dc91009d656d09fc58a838e1.png" height="600px" width="700px"/>  
</center>

```c++
// Server.cpp

int Server::main(const std::vector<std::string> & /*args*/)
{
    ...
    loadMetadataSystem(global_context);
    loadMetadata(global_context, default_database);
    database_catalog.loadDatabases();
    ...
}

// loadMetadata.cpp
// 加载 system database
void loadMetadataSystem(ContextMutablePtr context)
{
    String path = context->getPath() + "metadata/" + DatabaseCatalog::SYSTEM_DATABASE;
    String metadata_file = path + ".sql";   // "/tmp/clickhouse/rep1/metadata/system.sql"
    if (fs::exists(fs::path(path)) || fs::exists(fs::path(metadata_file)))
    {
        /// 'has_force_restore_data_flag' is true, to not fail on loading query_log table, if it is corrupted.
        loadDatabase(context, DatabaseCatalog::SYSTEM_DATABASE, path, true);
    }
    else
    {
        /// Initialize system database manually
        String database_create_query = "CREATE DATABASE ";
        database_create_query += DatabaseCatalog::SYSTEM_DATABASE;
        database_create_query += " ENGINE=Atomic";
        executeCreateQuery(database_create_query, context, DatabaseCatalog::SYSTEM_DATABASE, "<no file>", true);
    }

}

// loadMetadata.cpp
static void loadDatabase(
    ContextMutablePtr context,
    const String & database,
    const String & database_path,
    bool force_restore_data)
{
    String database_attach_query;
    String database_metadata_file = database_path + ".sql"  // "/tmp/clickhouse/rep1/metadata/system.sql"

    if (fs::exists(fs::path(database_metadata_file)))
    {
        /// There is .sql file with database creation statement.
        ReadBufferFromFile in(database_metadata_file, 1024);
        readStringUntilEOF(database_attach_query, in);  // "ATTACH DATABASE _ UUID '3a06dd4d-32bf-458e-ba06-dd4d32bf458e'\nENGINE = Atomic\n"
    }
    ...

    try 
    {
        executeCreateQuery(database_attach_query, context, database, database_metadata_file, force_restore_data);
    }
    ...
}
```
executeCreateQuery 会创建相应的 InterpreterCreateQuery 对象
```c++
// InterpreterCreateQuery.cpp

BlockIO InterpreterCreateQuery::createDatabase(ASTCreateQuery & create)
{
    String database_name = create.name; // system

    auto guard = DatabaseCatalog::instance().getDDLGuard(database_name, "");

    /// Database can be created before or it can be created concurrently in another thread, while we were waiting in DDLGuard
    if (DatabaseCatalog::instance().isDatabaseExist(database_name))
    {
        if (create.if_not_exists)
            return {};
        else
            throw Exception("Database " + database_name + " already exists.", ErrorCodes::DATABASE_ALREADY_EXISTS);
    }
    ...
    auto db_guard = DatabaseCatalog::instance().getExclusiveDDLGuardForDatabase(database_name);

    bool added = false;
    bool renamed = false;
    try
    {
        /// TODO Attach db only after it was loaded. Now it's not possible because of view dependencies
        DatabaseCatalog::instance().attachDatabase(database_name, database);
        added = true;

        if (need_write_metadata)
        {
            /// Prevents from overwriting metadata of detached database
            renameNoReplace(metadata_file_tmp_path, metadata_file_path);
            renamed = true;
        }

        /// We use global context here, because storages lifetime is bigger than query context lifetime
        database->loadStoredObjects(getContext()->getGlobalContext(), has_force_restore_data_flag, create.attach && force_attach); //-V560
    }
    ...
}

// DatabaseAtomic.cpp

void DatabaseAtomic::loadStoredObjects(ContextMutablePtr local_context, bool has_force_restore_data_flag, bool force_attach)
{
    /// Recreate symlinks to table data dirs in case of force restore, because some of them may be broken
    if (has_force_restore_data_flag)
    {
        for (const auto & table_path : fs::directory_iterator(path_to_table_symlinks))
        {
            if (!fs::is_symlink(table_path))
            {
                throw Exception(ErrorCodes::ABORTED,
                    "'{}' is not a symlink. Atomic database should contains only symlinks.", std::string(table_path.path()));
            }

            fs::remove(table_path);
        }
    }

    DatabaseOrdinary::loadStoredObjects(local_context, has_force_restore_data_flag, force_attach);

    if (has_force_restore_data_flag)
    {
        NameToPathMap table_names;
        {
            std::lock_guard lock{mutex};
            table_names = table_name_to_path;
        }

        fs::create_directories(path_to_table_symlinks);
        for (const auto & table : table_names)
            tryCreateSymlink(table.first, table.second, true);
    }
}

// DatabaseOrdinary.cpp

void DatabaseOrdinary::loadStoredObjects(ContextMutablePtr local_context, bool has_force_restore_data_flag, bool /*force_attach*/)


// DatabaseOnDisk.cpp

void DatabaseOnDisk::iterateMetadataFiles(ContextPtr local_context, const IteratingFunction & process_metadata_file) const
{

}

```