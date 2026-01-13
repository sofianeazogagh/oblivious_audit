#include "data_loader.h"
#include <openssl/sha.h>
#include <iostream>
#include <iomanip>

// Function to print an entry_t value
void printEntry(const entry_t& val) {
    std::cout << val.toUnsignedLong();
}

int main(int argc, char* argv[]) {
    // ========================================================================
    // 1. Configuration
    // ========================================================================
    // Database precision in bits
    const uint64_t d = 1;
    
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <data_file> [query_index] [column_name]" << std::endl;
        std::cerr << "  data_file: path to CSV or Parquet file containing a column of numeric values" << std::endl;
        std::cerr << "  query_index: index of element to retrieve (default: 0)" << std::endl;
        std::cerr << "  column_name: column name (optional, for Parquet only)" << std::endl;
        std::cerr << std::endl;
        std::cerr << "Note: Precision d=" << d << " bits is defined in the source code." << std::endl;
        std::cerr << "      Valid values: [0, " << ((1ULL << d) - 1) << "]" << std::endl;
        std::cerr << "      Supported formats: .csv, .parquet" << std::endl;
        return 1;
    }
    
    const std::string dataFile = argv[1];
    const uint64_t queryIndex = (argc > 2) ? std::stoull(argv[2]) : 0;
    const std::string columnName = (argc > 3) ? argv[3] : "";
    
    // Detect file format
    FileFormat format = detectFileFormat(dataFile);
    
    if (format == FileFormat::UNKNOWN) {
        std::cerr << "Error: unrecognized file format. Supported formats: .csv, .parquet" << std::endl;
        return 1;
    }
    
    std::cout << "========================================" << std::endl;
    std::cout << "  VLHEPIR with " << (format == FileFormat::PARQUET ? "Parquet" : "CSV") << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "File: " << dataFile << std::endl;
    std::cout << "Format: " << (format == FileFormat::PARQUET ? "Parquet" : "CSV") << std::endl;
    std::cout << "Precision (d): " << d << " bits" << std::endl;
    std::cout << "Query index: " << queryIndex << std::endl;
    if (!columnName.empty()) {
        std::cout << "Column: " << columnName << std::endl;
    }
    std::cout << std::endl;
    
    // ========================================================================
    // 2. Analyze the file
    // ========================================================================
    std::cout << "=== File Analysis ===" << std::endl;
    if (format == FileFormat::PARQUET) {
        printParquetStats(dataFile, d, columnName);
    } else {
        printCSVStats(dataFile, d, true);
    }
    std::cout << std::endl;
    
    // ========================================================================
    // 3. Create PIR from file
    // ========================================================================
    std::cout << "=== Parameters instantiation ===" << std::endl;
    VLHEPIR pir = createVLHEPIRFromFile(
        dataFile,
        d,          // precision in bits
        columnName, // column name (for Parquet)
        true,       // hasHeader (for CSV)
        true,       // allowTrivial
        false,      // verbose (set to true to see detailed optimization)
        false,      // simplePIR
        1,          // batchSize
        false       // honestHint
    );
    
    std::cout << "Database parameters: ";
    pir.dbParams.print();
    std::cout << std::endl;
    
    // ========================================================================
    // 4. Prepare database for queries
    // ========================================================================
    std::cout << "=== Database Preparation ===" << std::endl;
    // First, pack database data into a matrix
    Matrix D = pir.db.packDataInMatrix(pir.dbParams, true);
    std::cout << "Database packed into matrix D (dimensions: " 
              << D.rows << " x " << D.cols << ")" << std::endl;
    
    // Then, pack the matrix for optimization
    PackedMatrix D_packed = packMatrixHardCoded(D, pir.lhe.p);
    std::cout << "Matrix D packed (dimensions: " 
              << D_packed.mat.rows << " x " << D_packed.mat.cols << ")" << std::endl;
    std::cout << std::endl;
    
    // ========================================================================
    // 5. Offline Phase (can be done once)
    // ========================================================================
    std::cout << "=== Offline Phase ===" << std::endl;
    
    // Generate public matrix A
    Matrix A = pir.Init();
    std::cout << "Public matrix A generated" << std::endl;
    
    // Generate hint H with the real matrix D (needed for Recover)
    Matrix H = pir.GenerateHint(A, D);
    std::cout << "Hint H generated" << std::endl;
    std::cout << "Hint size: " 
              << H.rows * H.cols * sizeof(Elem) / (1ULL << 20) 
              << " MiB" << std::endl;
    
    // Hash A and H for proof generation (needed for verification)
    unsigned char hash[SHA256_DIGEST_LENGTH];
    pir.HashAandH(hash, A, H);
    
    std::cout << std::endl;
    
    // ========================================================================
    // 6. Online Phase - Generate query
    // ========================================================================
    std::cout << "=== Online Phase - Query ===" << std::endl;
    
    if (queryIndex >= pir.N) {
        std::cerr << "Error: index " << queryIndex 
                  << " out of bounds (max: " << (pir.N - 1) << ")" << std::endl;
        return 1;
    }
    
    // Expected value (for verification)
    entry_t expectedValue = pir.db.getDataAtIndex(queryIndex);
    std::cout << "Expected value at index " << queryIndex << ": ";
    printEntry(expectedValue);
    std::cout << std::endl;
    
    // Generate query
    
    auto ct_sk = pir.Query(A, queryIndex);
    Matrix ct = std::get<0>(ct_sk);  // Ciphertext (encrypted query)
    Matrix sk = std::get<1>(ct_sk);  // Secret key (for decryption)
    
    std::cout << "Query generated for index " << queryIndex << std::endl;
    std::cout << "Query size: " 
              << ct.rows * ct.cols * sizeof(Elem) / 1024.0 
              << " KiB" << std::endl;
    std::cout << std::endl;
    
    // ========================================================================
    // 7. Online Phase - Generate answer (server side)
    // ========================================================================
    std::cout << "=== Online Phase - Answer ===" << std::endl;
    
    Matrix ans = pir.Answer(ct, D_packed);
    std::cout << "Answer generated" << std::endl;
    std::cout << "Answer size: " 
              << ans.rows * ans.cols * sizeof(Elem) / 1024.0 
              << " KiB" << std::endl;
    std::cout << std::endl;
    
    // ========================================================================
    // 8. Online Phase - Verification
    // ========================================================================
    std::cout << "=== Online Phase - Verification ===" << std::endl;
    
    // Generate proof Z for real
    Matrix Z = pir.Prove(hash, ct, ans, D_packed);
    
    // Verify proof with Verify (real verification with fake=false)
    pir.Verify(A, H, hash, ct, ans, Z, false);
    std::cout << "Verification successful ✓" << std::endl;
    std::cout << std::endl;
    
    // ========================================================================
    // 9. Online Phase - Result recovery (client side)
    // ========================================================================
    std::cout << "=== Online Phase - Recovery ===" << std::endl;
    
    entry_t result = pir.Recover(H, ans, sk, queryIndex);
    
    std::cout << "Recovered result: ";
    printEntry(result);
    std::cout << std::endl;
    
    // ========================================================================
    // 10. Verification
    // ========================================================================
    std::cout << std::endl;
    std::cout << "=== Verification ===" << std::endl;
    if (result == expectedValue) {
        std::cout << "✓ Success! Recovered value matches expected value." 
                  << std::endl;
    } else {
        std::cout << "✗ Error! Expected value: ";
        printEntry(expectedValue);
        std::cout << ", Recovered value: ";
        printEntry(result);
        std::cout << std::endl;
        return 1;
    }
    
    std::cout << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "  PIR query completed successfully!" << std::endl;
    std::cout << "========================================" << std::endl;
    
    return 0;
}