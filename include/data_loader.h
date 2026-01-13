#ifndef DATA_LOADER_H
#define DATA_LOADER_H

#include "pir/database.h"
#include "pir/pir.h"
#include <string>
#include <cstdint>

// ============================================================================
// Utility functions
// ============================================================================

/**
 * Counts the number of lines in a CSV file (excluding header)
 */
uint64_t countCSVLines(const std::string& csvFilePath, bool hasHeader = true);

/**
 * Determines the bit size needed to store a value
 */
uint64_t calculateBitSize(uint64_t value);

/**
 * Verifies that all values in the column are valid for d bits
 * If d=1, checks that values are 0 or 1
 * Otherwise, checks that values are in [0, 2^d-1]
 */
bool validateColumnForD(const std::string& csvFilePath, 
                        uint64_t d,
                        bool hasHeader = true);

// ============================================================================
// Database loading functions
// ============================================================================

/**
 * Loads CSV column data into a Database
 * Values must be in [0, 2^d-1]
 */
bool loadDatabaseFromCSV(Database& db,
                         const std::string& csvFilePath,
                         uint64_t d,
                         bool hasHeader = true,
                         uint64_t maxRows = 0);

// ============================================================================
// PIR creation functions from CSV
// ============================================================================

/**
 * Creates a VLHEPIR from a CSV file
 * Automatically determines N, d must be specified
 */
VLHEPIR createVLHEPIRFromCSV(const std::string& csvFilePath,
                             uint64_t d,
                             bool hasHeader = true,
                             bool allowTrivial = true,
                             bool verbose = false,
                             bool simplePIR = false,
                             uint64_t batchSize = 1,
                             bool honestHint = false);

/**
 * Prints statistics about a CSV file
 */
void printCSVStats(const std::string& csvFilePath,
                   uint64_t d,
                   bool hasHeader = true);

// ============================================================================
// Functions for Parquet files
// ============================================================================

/**
 * Counts the number of lines in a Parquet file
 */
uint64_t countParquetLines(const std::string& parquetFilePath, const std::string& columnName = "");

/**
 * Verifies that all values in the Parquet column are valid for d bits
 */
bool validateParquetColumnForD(const std::string& parquetFilePath,
                               uint64_t d,
                               const std::string& columnName = "");

/**
 * Loads Parquet column data into a Database
 */
bool loadDatabaseFromParquet(Database& db,
                             const std::string& parquetFilePath,
                             uint64_t d,
                             const std::string& columnName = "",
                             uint64_t maxRows = 0);

/**
 * Creates a VLHEPIR from a Parquet file
 */
VLHEPIR createVLHEPIRFromParquet(const std::string& parquetFilePath,
                                 uint64_t d,
                                 const std::string& columnName = "",
                                 bool allowTrivial = true,
                                 bool verbose = false,
                                 bool simplePIR = false,
                                 uint64_t batchSize = 1,
                                 bool honestHint = false);

/**
 * Prints statistics about a Parquet file
 */
void printParquetStats(const std::string& parquetFilePath,
                      uint64_t d,
                      const std::string& columnName = "");

/**
 * Detects the file format (CSV or Parquet)
 */
enum class FileFormat { CSV, PARQUET, UNKNOWN };
FileFormat detectFileFormat(const std::string& filePath);

/**
 * Creates a VLHEPIR from a file (automatic format detection)
 */
VLHEPIR createVLHEPIRFromFile(const std::string& filePath,
                              uint64_t d,
                              const std::string& columnName = "",
                              bool hasHeader = true,
                              bool allowTrivial = true,
                              bool verbose = false,
                              bool simplePIR = false,
                              uint64_t batchSize = 1,
                              bool honestHint = false);

#endif // DATA_LOADER_H