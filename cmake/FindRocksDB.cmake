# FindRocksDB.cmake

# Locate the rocksdb library
find_path(ROCKSDB_INCLUDE_DIR rocksdb/db.h)
find_library(ROCKSDB_LIBRARY NAMES rocksdb)

# Handle the QUIETLY and REQUIRED arguments and set ROCKSDB_FOUND to TRUE if all listed variables are TRUE
include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(RocksDB DEFAULT_MSG
                                  ROCKSDB_INCLUDE_DIR
                                  ROCKSDB_LIBRARY)

if(ROCKSDB_FOUND)
  set(ROCKSDB_LIBRARIES ${ROCKSDB_LIBRARY})
  set(ROCKSDB_INCLUDE_DIRS ${ROCKSDB_INCLUDE_DIR})
endif()
