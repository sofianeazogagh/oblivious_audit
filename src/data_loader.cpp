#include "data_loader.h"
#include "pir/database.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>

// Parquet support with Apache Arrow
#ifdef PARQUET_SUPPORT
#include <arrow/api.h>
#include <arrow/io/api.h>
#include <parquet/arrow/reader.h>
#include <parquet/arrow/writer.h>
#include <parquet/stream_reader.h>
#include <parquet/stream_writer.h>
#endif

// ============================================================================
// Utility functions
// ============================================================================

/**
 * Counts the number of lines in a CSV file (excluding header)
 */
uint64_t countCSVLines(const std::string& csvFilePath, bool hasHeader) {
    std::ifstream file(csvFilePath);
    if (!file.is_open()) {
        std::cerr << "Error: unable to open file " << csvFilePath << std::endl;
        return 0;
    }
    
    std::string line;
    uint64_t count = 0;
    
    // Skip header if present
    if (hasHeader && std::getline(file, line)) {
        // Header line ignored
    }
    
    // Count data lines
    while (std::getline(file, line)) {
        // Ignore empty lines
        if (!line.empty() && line.find_first_not_of(" \t\r\n") != std::string::npos) {
            count++;
        }
    }
    
    file.close();
    return count;
}

/**
 * Determines the bit size needed to store a value
 */
uint64_t calculateBitSize(uint64_t value) {
    if (value == 0) return 1;
    return static_cast<uint64_t>(std::floor(std::log2(value)) + 1);
}

/**
 * Verifies that all values in the column are valid for d bits
 * If d=1, checks that values are 0 or 1
 * Otherwise, checks that values are in [0, 2^d-1]
 */
bool validateColumnForD(const std::string& csvFilePath, 
                        uint64_t d,
                        bool hasHeader) {
    std::ifstream file(csvFilePath);
    if (!file.is_open()) {
        std::cerr << "Error: unable to open file " << csvFilePath << std::endl;
        return false;
    }
    
    std::string line;
    uint64_t rowCount = 0;
    entry_t maxValue = (entry_t(1) << d) - entry_t(1);
    
    // Skip header
    if (hasHeader && std::getline(file, line)) {
        // Header line ignored
    }
    
    while (std::getline(file, line)) {
        if (line.empty()) continue;
        
        std::stringstream ss(line);
        std::string cell;
        
        // Take the first column
        if (std::getline(ss, cell, ',')) {
            // Remove spaces
            cell.erase(0, cell.find_first_not_of(" \t\r\n"));
            cell.erase(cell.find_last_not_of(" \t\r\n") + 1);
            
            if (!cell.empty()) {
                try {
                    uint64_t value = std::stoull(cell);
                    entry_t entryValue = entry_t(static_cast<unsigned long>(value));
                    
                    if (entryValue > maxValue) {
                        std::cerr << "Error: value too large found at line " 
                                  << (rowCount + 1) << ": " << cell 
                                  << " (max for d=" << d << ": " << maxValue.toUnsignedLong() << ")" << std::endl;
                        file.close();
                        return false;
                    }
                } catch (const std::exception& e) {
                    std::cerr << "Error: non-numeric value at line " 
                              << (rowCount + 1) << ": " << cell << std::endl;
                    file.close();
                    return false;
                }
            }
        }
        rowCount++;
    }
    
    file.close();
    return true;
}

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
                         bool hasHeader,
                         uint64_t maxRows) {
    // Allocate memory if necessary
    if (!db.alloc) {
        db.data = (entry_t*)malloc(db.N * sizeof(entry_t));
        db.alloc = true;
    }
    
    if (!db.data) {
        std::cerr << "Error: memory allocation failed" << std::endl;
        return false;
    }
    
    memset(db.data, 0, db.N * sizeof(entry_t));
    
    std::ifstream file(csvFilePath);
    if (!file.is_open()) {
        std::cerr << "Error: unable to open file " << csvFilePath << std::endl;
        return false;
    }
    
    std::string line;
    uint64_t index = 0;
    entry_t modulus = entry_t(1) << d;
    entry_t maxValue = modulus - entry_t(1);
    
    // Skip header if present
    if (hasHeader && std::getline(file, line)) {
        // Header line ignored
    }
    
    // Load data (first column only)
    while (std::getline(file, line) && index < db.N && (maxRows == 0 || index < maxRows)) {
        if (line.empty()) continue;
        
        std::stringstream ss(line);
        std::string cell;
        
        // Take the first column
        if (std::getline(ss, cell, ',') && !cell.empty()) {
            try {
                // Remove spaces
                cell.erase(0, cell.find_first_not_of(" \t\r\n"));
                cell.erase(cell.find_last_not_of(" \t\r\n") + 1);
                
                if (!cell.empty()) {
                    uint64_t value = std::stoull(cell);
                    entry_t entryValue = entry_t(static_cast<unsigned long>(value));
                    
                    if (entryValue > maxValue) {
                        std::cerr << "Warning: value too large at line " 
                                  << (index + 1) << ": " << cell 
                                  << " (max for d=" << d << ": " << maxValue.toUnsignedLong() 
                                  << ", used " << maxValue.toUnsignedLong() << ")" << std::endl;
                        db.data[index] = maxValue;
                    } else {
                        db.data[index] = entryValue % modulus;
                    }
                } else {
                    // Empty value, set to 0
                    db.data[index] = entry_t(0);
                }
            } catch (const std::exception& e) {
                // If not a number, use 0
                std::cerr << "Warning: non-numeric value at line " 
                          << (index + 1) << ": " << cell << " (used 0)" << std::endl;
                db.data[index] = entry_t(0);
            }
        } else {
            // No column found, set to 0
            db.data[index] = entry_t(0);
        }
        
        index++;
    }
    
    file.close();
    
    if (index < db.N) {
        std::cerr << "Warning: only " << index 
                  << " lines loaded out of " << db.N << " expected" << std::endl;
    }
    
    return true;
}

// ============================================================================
// PIR creation functions from CSV
// ============================================================================

/**
 * Creates a VeriSimplePIR from a CSV file
 * Automatically determines N, d must be specified
 */
VLHEPIR createVLHEPIRFromCSV(const std::string& csvFilePath,
                             uint64_t d,
                             bool hasHeader,
                             bool allowTrivial,
                             bool verbose,
                             bool simplePIR,
                             uint64_t batchSize,
                             bool honestHint) {
    // 1. Count the number of lines
    uint64_t N = countCSVLines(csvFilePath, hasHeader);
    if (N == 0) {
        std::cerr << "Error: no data found in CSV" << std::endl;
        exit(1);
    }
    
    // 2. Verify that all values are valid for d bits
    if (!validateColumnForD(csvFilePath, d, hasHeader)) {
        entry_t maxValue = (entry_t(1) << d) - entry_t(1);
        std::cerr << "Error: CSV must contain only values in [0, " 
                  << maxValue.toUnsignedLong() << "] for d=" << d << std::endl;
        exit(1);
    }
    
    if (verbose) {
        std::cout << "CSV Analysis:" << std::endl;
        std::cout << "  Number of elements (N): " << N << std::endl;
        std::cout << "  Bit size (d): " << d << std::endl;
        std::cout << "  Database size: " << (N * d) / (8.0 * (1ULL << 20)) << " MiB" << std::endl;
    }
    
    // 3. Create the database
    Database db(N, d);
    
    // 4. Load data from CSV
    if (!loadDatabaseFromCSV(db, csvFilePath, d, hasHeader)) {
        std::cerr << "Error: CSV loading failed" << std::endl;
        exit(1);
    }
    
    // 5. Create the PIR
    VLHEPIR pir(
        N, d,
        allowTrivial,
        verbose,
        simplePIR,
        false,      // randomData = false (loading from CSV)
        batchSize,
        honestHint
    );
    
    // 6. Copy data into pir.db
    // Note: pir.db is already created in the constructor, we need to copy the data
    if (pir.db.alloc) {
        free(pir.db.data);
    }
    pir.db.data = (entry_t*)malloc(N * sizeof(entry_t));
    pir.db.alloc = true;
    memcpy(pir.db.data, db.data, N * sizeof(entry_t));
    
    return pir;
}
/**
 * Prints statistics about a CSV file
 */
void printCSVStats(const std::string& csvFilePath,
                   uint64_t d,
                   bool hasHeader) {
    uint64_t N = countCSVLines(csvFilePath, hasHeader);
    entry_t maxValue = (entry_t(1) << d) - entry_t(1);
    
    // Find min and max
    uint64_t minVal = UINT64_MAX, maxVal = 0;
    std::ifstream file(csvFilePath);
    if (file.is_open()) {
        std::string line;
        if (hasHeader && std::getline(file, line)) {
            // Skip header
        }
        while (std::getline(file, line)) {
            if (line.empty()) continue;
            std::stringstream ss(line);
            std::string cell;
            if (std::getline(ss, cell, ',')) {
                cell.erase(0, cell.find_first_not_of(" \t\r\n"));
                cell.erase(cell.find_last_not_of(" \t\r\n") + 1);
                if (!cell.empty()) {
                    try {
                        uint64_t value = std::stoull(cell);
                        if (value < minVal) minVal = value;
                        if (value > maxVal) maxVal = value;
                    } catch (...) {
                        // Ignore
                    }
                }
            }
        }
        file.close();
    }
    
    std::cout << "=== CSV Statistics ===" << std::endl;
    std::cout << "File: " << csvFilePath << std::endl;
    std::cout << "Number of lines (N): " << N << std::endl;
    std::cout << "Bit size (d): " << d << std::endl;
    std::cout << "Maximum allowed value: " << maxValue.toUnsignedLong() << std::endl;
    if (minVal != UINT64_MAX) {
        std::cout << "Minimum value found: " << minVal << std::endl;
        std::cout << "Maximum value found: " << maxVal << std::endl;
    }
    std::cout << "Database size: " << (N * d) / (8.0 * (1ULL << 20)) << " MiB" << std::endl;
    std::cout << "===============================" << std::endl;
}

// ============================================================================
// Functions for Parquet files
// ============================================================================

FileFormat detectFileFormat(const std::string& filePath) {
    // Extract file extension
    size_t lastDot = filePath.find_last_of('.');
    if (lastDot == std::string::npos) {
        return FileFormat::UNKNOWN;
    }
    
    std::string ext = filePath.substr(lastDot);
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    
    if (ext == ".csv") {
        return FileFormat::CSV;
    } else if (ext == ".parquet") {
        return FileFormat::PARQUET;
    }
    return FileFormat::UNKNOWN;
}

#ifdef PARQUET_SUPPORT

uint64_t countParquetLines(const std::string& parquetFilePath, const std::string& columnName) {
    try {
        auto infile_result = arrow::io::ReadableFile::Open(parquetFilePath);
        if (!infile_result.ok()) {
            std::cerr << "Error: unable to open Parquet file " << parquetFilePath << std::endl;
            return 0;
        }
        std::shared_ptr<arrow::io::ReadableFile> infile = infile_result.ValueOrDie();

        auto reader_result = parquet::arrow::OpenFile(infile, arrow::default_memory_pool());
        if (!reader_result.ok()) {
            std::cerr << "Error: unable to read Parquet file" << std::endl;
            return 0;
        }
        std::unique_ptr<parquet::arrow::FileReader> reader = std::move(reader_result.ValueOrDie());

        std::shared_ptr<arrow::Table> table;
        arrow::Status status = reader->ReadTable(&table);
        if (!status.ok()) {
            std::cerr << "Error: unable to read Parquet table" << std::endl;
            return 0;
        }

        return table->num_rows();
    } catch (const std::exception& e) {
        std::cerr << "Error reading Parquet file: " << e.what() << std::endl;
        return 0;
    }
}

bool validateParquetColumnForD(const std::string& parquetFilePath,
                               uint64_t d,
                               const std::string& columnName) {
    try {
        auto infile_result = arrow::io::ReadableFile::Open(parquetFilePath);
        if (!infile_result.ok()) {
            std::cerr << "Error: unable to open Parquet file" << std::endl;
            return false;
        }
        std::shared_ptr<arrow::io::ReadableFile> infile = infile_result.ValueOrDie();

        auto reader_result = parquet::arrow::OpenFile(infile, arrow::default_memory_pool());
        if (!reader_result.ok()) {
            return false;
        }
        std::unique_ptr<parquet::arrow::FileReader> reader = std::move(reader_result.ValueOrDie());

        std::shared_ptr<arrow::Table> table;
        arrow::Status status = reader->ReadTable(&table);
        if (!status.ok()) {
            return false;
        }

        std::string colName = columnName.empty() ? table->schema()->field(0)->name() : columnName;
        std::shared_ptr<arrow::ChunkedArray> column = table->GetColumnByName(colName);
        if (!column) {
            std::cerr << "Error: column '" << colName << "' not found" << std::endl;
            return false;
        }

        entry_t maxValue = (entry_t(1) << d) - entry_t(1);
        
        for (int chunk_idx = 0; chunk_idx < column->num_chunks(); chunk_idx++) {
            std::shared_ptr<arrow::Array> chunk = column->chunk(chunk_idx);
            
            if (chunk->type_id() == arrow::Type::INT64) {
                auto int64_array = std::static_pointer_cast<arrow::Int64Array>(chunk);
                for (int64_t i = 0; i < int64_array->length(); i++) {
                    if (int64_array->IsNull(i)) continue;
                    int64_t value = int64_array->Value(i);
                    if (value < 0 || entry_t(static_cast<unsigned long>(value)) > maxValue) {
                        std::cerr << "Error: invalid value found: " << value << std::endl;
                        return false;
                    }
                }
            } else if (chunk->type_id() == arrow::Type::UINT64) {
                auto uint64_array = std::static_pointer_cast<arrow::UInt64Array>(chunk);
                for (int64_t i = 0; i < uint64_array->length(); i++) {
                    if (uint64_array->IsNull(i)) continue;
                    uint64_t value = uint64_array->Value(i);
                    if (entry_t(static_cast<unsigned long>(value)) > maxValue) {
                        std::cerr << "Error: invalid value found: " << value << std::endl;
                        return false;
                    }
                }
            } else {
                std::cerr << "Error: unsupported column type (must be INT64 or UINT64)" << std::endl;
                return false;
            }
        }

        return true;
    } catch (const std::exception& e) {
        std::cerr << "Error during Parquet validation: " << e.what() << std::endl;
        return false;
    }
}

bool loadDatabaseFromParquet(Database& db,
                            const std::string& parquetFilePath,
                            uint64_t d,
                            const std::string& columnName,
                            uint64_t maxRows) {
    try {
        auto infile_result = arrow::io::ReadableFile::Open(parquetFilePath);
        if (!infile_result.ok()) {
            std::cerr << "Error: unable to open Parquet file" << std::endl;
            return false;
        }
        std::shared_ptr<arrow::io::ReadableFile> infile = infile_result.ValueOrDie();

        auto reader_result = parquet::arrow::OpenFile(infile, arrow::default_memory_pool());
        if (!reader_result.ok()) {
            return false;
        }
        std::unique_ptr<parquet::arrow::FileReader> reader = std::move(reader_result.ValueOrDie());

        std::shared_ptr<arrow::Table> table;
        arrow::Status status = reader->ReadTable(&table);
        if (!status.ok()) {
            return false;
        }

        std::string colName = columnName.empty() ? table->schema()->field(0)->name() : columnName;
        std::shared_ptr<arrow::ChunkedArray> column = table->GetColumnByName(colName);
        if (!column) {
            std::cerr << "Error: column '" << colName << "' not found" << std::endl;
            return false;
        }

        uint64_t N = std::min(static_cast<uint64_t>(table->num_rows()), maxRows > 0 ? maxRows : UINT64_MAX);
        
        if (!db.alloc) {
            db.data = (entry_t*)malloc(N * sizeof(entry_t));
            db.alloc = true;
        }

        uint64_t idx = 0;
        for (int chunk_idx = 0; chunk_idx < column->num_chunks() && idx < N; chunk_idx++) {
            std::shared_ptr<arrow::Array> chunk = column->chunk(chunk_idx);
            
            if (chunk->type_id() == arrow::Type::INT64) {
                auto int64_array = std::static_pointer_cast<arrow::Int64Array>(chunk);
                for (int64_t i = 0; i < int64_array->length() && idx < N; i++) {
                    if (int64_array->IsNull(i)) {
                        db.data[idx] = entry_t(0);
                    } else {
                        int64_t value = int64_array->Value(i);
                        db.data[idx] = entry_t(static_cast<unsigned long>(std::max(0LL, value)));
                    }
                    idx++;
                }
            } else if (chunk->type_id() == arrow::Type::UINT64) {
                auto uint64_array = std::static_pointer_cast<arrow::UInt64Array>(chunk);
                for (int64_t i = 0; i < uint64_array->length() && idx < N; i++) {
                    if (uint64_array->IsNull(i)) {
                        db.data[idx] = entry_t(0);
                    } else {
                        uint64_t value = uint64_array->Value(i);
                        db.data[idx] = entry_t(static_cast<unsigned long>(value));
                    }
                    idx++;
                }
            }
        }

        return true;
    } catch (const std::exception& e) {
        std::cerr << "Error loading Parquet: " << e.what() << std::endl;
        return false;
    }
}

VLHEPIR createVLHEPIRFromParquet(const std::string& parquetFilePath,
                                uint64_t d,
                                const std::string& columnName,
                                bool allowTrivial,
                                bool verbose,
                                bool simplePIR,
                                uint64_t batchSize,
                                bool honestHint) {
    uint64_t N = countParquetLines(parquetFilePath, columnName);
    if (N == 0) {
        std::cerr << "Error: no data found in Parquet file" << std::endl;
        exit(1);
    }

    if (!validateParquetColumnForD(parquetFilePath, d, columnName)) {
        entry_t maxValue = (entry_t(1) << d) - entry_t(1);
        std::cerr << "Error: Parquet file must contain only values in [0, " 
                  << maxValue.toUnsignedLong() << "] for d=" << d << std::endl;
        exit(1);
    }

    if (verbose) {
        std::cout << "Parquet Analysis:" << std::endl;
        std::cout << "  Number of elements (N): " << N << std::endl;
        std::cout << "  Bit size (d): " << d << std::endl;
        std::cout << "  Database size: " << (N * d) / (8.0 * (1ULL << 20)) << " MiB" << std::endl;
    }

    Database db(N, d);
    if (!loadDatabaseFromParquet(db, parquetFilePath, d, columnName)) {
        std::cerr << "Error: Parquet file loading failed" << std::endl;
        exit(1);
    }

    VLHEPIR pir(N, d, allowTrivial, verbose, simplePIR, false, batchSize, honestHint);
    
    if (pir.db.alloc) {
        free(pir.db.data);
    }
    pir.db.data = (entry_t*)malloc(N * sizeof(entry_t));
    pir.db.alloc = true;
    memcpy(pir.db.data, db.data, N * sizeof(entry_t));
    
    return pir;
}

void printParquetStats(const std::string& parquetFilePath,
                      uint64_t d,
                      const std::string& columnName) {
    try {
        auto infile_result = arrow::io::ReadableFile::Open(parquetFilePath);
        if (!infile_result.ok()) {
            std::cerr << "Error: unable to open Parquet file" << std::endl;
            return;
        }
        std::shared_ptr<arrow::io::ReadableFile> infile = infile_result.ValueOrDie();

        auto reader_result = parquet::arrow::OpenFile(infile, arrow::default_memory_pool());
        if (!reader_result.ok()) {
            return;
        }
        std::unique_ptr<parquet::arrow::FileReader> reader = std::move(reader_result.ValueOrDie());

        std::shared_ptr<arrow::Table> table;
        arrow::Status status = reader->ReadTable(&table);
        if (!status.ok()) {
            return;
        }

        std::string colName = columnName.empty() ? table->schema()->field(0)->name() : columnName;
        std::shared_ptr<arrow::ChunkedArray> column = table->GetColumnByName(colName);
        if (!column) {
            std::cerr << "Error: column '" << colName << "' not found" << std::endl;
            return;
        }

        uint64_t N = table->num_rows();
        entry_t maxValue = (entry_t(1) << d) - entry_t(1);
        uint64_t minVal = UINT64_MAX, maxVal = 0;

        for (int chunk_idx = 0; chunk_idx < column->num_chunks(); chunk_idx++) {
            std::shared_ptr<arrow::Array> chunk = column->chunk(chunk_idx);
            
            if (chunk->type_id() == arrow::Type::INT64) {
                auto int64_array = std::static_pointer_cast<arrow::Int64Array>(chunk);
                for (int64_t i = 0; i < int64_array->length(); i++) {
                    if (int64_array->IsNull(i)) continue;
                    int64_t value = int64_array->Value(i);
                    uint64_t uvalue = static_cast<uint64_t>(std::max(0LL, value));
                    if (uvalue < minVal) minVal = uvalue;
                    if (uvalue > maxVal) maxVal = uvalue;
                }
            } else if (chunk->type_id() == arrow::Type::UINT64) {
                auto uint64_array = std::static_pointer_cast<arrow::UInt64Array>(chunk);
                for (int64_t i = 0; i < uint64_array->length(); i++) {
                    if (uint64_array->IsNull(i)) continue;
                    uint64_t value = uint64_array->Value(i);
                    if (value < minVal) minVal = value;
                    if (value > maxVal) maxVal = value;
                }
            }
        }

        std::cout << "=== Parquet Statistics ===" << std::endl;
        std::cout << "File: " << parquetFilePath << std::endl;
        std::cout << "Column: " << colName << std::endl;
        std::cout << "Number of lines (N): " << N << std::endl;
        std::cout << "Bit size (d): " << d << std::endl;
        std::cout << "Maximum allowed value: " << maxValue.toUnsignedLong() << std::endl;
        if (minVal != UINT64_MAX) {
            std::cout << "Minimum value found: " << minVal << std::endl;
            std::cout << "Maximum value found: " << maxVal << std::endl;
        }
        std::cout << "Database size: " << (N * d) / (8.0 * (1ULL << 20)) << " MiB" << std::endl;
        std::cout << "===============================" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Error during Parquet analysis: " << e.what() << std::endl;
    }
}

#else

// Stubs when Parquet is not available
uint64_t countParquetLines(const std::string&, const std::string&) {
    std::cerr << "Error: Parquet support not compiled. Install Apache Arrow C++ and recompile with -DPARQUET_SUPPORT" << std::endl;
    return 0;
}

bool validateParquetColumnForD(const std::string&, uint64_t, const std::string&) {
    std::cerr << "Error: Parquet support not compiled" << std::endl;
    return false;
}

bool loadDatabaseFromParquet(Database&, const std::string&, uint64_t, const std::string&, uint64_t) {
    std::cerr << "Error: Parquet support not compiled" << std::endl;
    return false;
}

VLHEPIR createVLHEPIRFromParquet(const std::string&, uint64_t, const std::string&, bool, bool, bool, uint64_t, bool) {
    std::cerr << "Error: Parquet support not compiled" << std::endl;
    exit(1);
}

void printParquetStats(const std::string&, uint64_t, const std::string&) {
    std::cerr << "Error: Parquet support not compiled" << std::endl;
}

#endif // PARQUET_SUPPORT

VLHEPIR createVLHEPIRFromFile(const std::string& filePath,
                              uint64_t d,
                              const std::string& columnName,
                              bool hasHeader,
                              bool allowTrivial,
                              bool verbose,
                              bool simplePIR,
                              uint64_t batchSize,
                              bool honestHint) {
    FileFormat format = detectFileFormat(filePath);
    
    switch (format) {
        case FileFormat::CSV:
            return createVLHEPIRFromCSV(filePath, d, hasHeader, allowTrivial, verbose, simplePIR, batchSize, honestHint);
        case FileFormat::PARQUET:
            return createVLHEPIRFromParquet(filePath, d, columnName, allowTrivial, verbose, simplePIR, batchSize, honestHint);
        default:
            std::cerr << "Error: unrecognized file format. Supported formats: .csv, .parquet" << std::endl;
            exit(1);
    }
}