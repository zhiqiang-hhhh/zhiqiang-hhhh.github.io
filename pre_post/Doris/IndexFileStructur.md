```cpp
IndexFileWriter::close() {
    ...
    RETURN_IF_ERROR(_index_storage_format->write());
    ...
}

IndexStorageFileFormatV2::write() {
    // Calculate header length and initialize offset
    int64_t current_offset = header_length();
    // Prepare file metadata
    auto file_metadata = prepare_file_metadata(current_offset);
}
```