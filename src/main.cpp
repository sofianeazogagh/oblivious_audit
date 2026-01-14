#include "data_loader.h"
#include <openssl/sha.h>
#include <iostream>
#include <iomanip>
#include <chrono>

const bool verify = false;

// Function to print an entry_t value
void printEntry(const entry_t& val) {
    std::cout << val.toUnsignedLong();
}

/**
 * Parse N from string, accepting either direct number or 2^n format
 * Returns (N, n) where N = 2^n, or (N, 0) if N is not a power of 2
 */
std::pair<uint64_t, uint64_t> parseN(const std::string& str) {
    // Check for 2^n or 2**n format
    size_t caret_pos = str.find('^');
    size_t star_pos = str.find("**");
    
    if (caret_pos != std::string::npos) {
        // Format: 2^n
        std::string base_str = str.substr(0, caret_pos);
        std::string exp_str = str.substr(caret_pos + 1);
        
        try {
            uint64_t base = std::stoull(base_str);
            uint64_t exp = std::stoull(exp_str);
            
            if (base == 2) {
                uint64_t N = 1ULL << exp;  // 2^exp
                return {N, exp};
            } else {
                // Not 2^n, just calculate base^exp
                uint64_t N = 1;
                for (uint64_t i = 0; i < exp; i++) {
                    N *= base;
                }
                return {N, 0};
            }
        } catch (...) {
            // Fall through to direct number parsing
        }
    } else if (star_pos != std::string::npos) {
        // Format: 2**n
        std::string base_str = str.substr(0, star_pos);
        std::string exp_str = str.substr(star_pos + 2);
        
        try {
            uint64_t base = std::stoull(base_str);
            uint64_t exp = std::stoull(exp_str);
            
            if (base == 2) {
                uint64_t N = 1ULL << exp;  // 2^exp
                return {N, exp};
            } else {
                // Not 2^n, just calculate base^exp
                uint64_t N = 1;
                for (uint64_t i = 0; i < exp; i++) {
                    N *= base;
                }
                return {N, 0};
            }
        } catch (...) {
            // Fall through to direct number parsing
        }
    }
    
    // Direct number format
    try {
        uint64_t N = std::stoull(str);
        // Check if N is a power of 2
        if (N > 0 && (N & (N - 1)) == 0) {
            // N is a power of 2, find the exponent
            uint64_t n = 0;
            uint64_t temp = N;
            while (temp > 1) {
                temp >>= 1;
                n++;
            }
            return {N, n};
        }
        return {N, 0};
    } catch (...) {
        throw std::invalid_argument("Invalid number format: " + str);
    }
}

int main(int argc, char* argv[]) {
    // ========================================================================
    // 1. Configuration
    // ========================================================================
    // Database precision in bits
    const uint64_t d = 1;
    
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <data_file> [query_index] [column_name]" << std::endl;
        std::cerr << "   OR: " << argv[0] << " --generate <N> <d> [query_index]" << std::endl;
        std::cerr << std::endl;
        std::cerr << "Options:" << std::endl;
        std::cerr << "  <data_file>: path to CSV or Parquet file containing a column of numeric values" << std::endl;
        std::cerr << "  --generate: generate a random database of N elements with d bits" << std::endl;
        std::cerr << "  <N>: number of elements in the database" << std::endl;
        std::cerr << "       Can be a number (e.g., 1024) or power of 2 (e.g., 2^10, 2**10)" << std::endl;
        std::cerr << "  <d>: number of bits per element (values in [0, 2^d-1])" << std::endl;
        std::cerr << "  query_index: index of element to retrieve (default: 0)" << std::endl;
        std::cerr << "  column_name: column name (optional, for Parquet only)" << std::endl;
        std::cerr << std::endl;
        std::cerr << "Examples:" << std::endl;
        std::cerr << "  " << argv[0] << " data/test.csv 5" << std::endl;
        std::cerr << "  " << argv[0] << " --generate 1000 1 5" << std::endl;
        std::cerr << "  " << argv[0] << " --generate 2^10 1 5" << std::endl;
        std::cerr << "  " << argv[0] << " --generate 2**20 8 42" << std::endl;
        return 1;
    }
    
    bool useRandomGeneration = false;
    uint64_t N = 0;
    uint64_t n = 0;  // exponent if N = 2^n
    bool N_is_power_of_2 = false;
    uint64_t d_value = d;
    uint64_t queryIndex = 0;
    std::string dataFile;
    std::string columnName = "";
    
    // Check if --generate option is used
    if (std::string(argv[1]) == "--generate" || std::string(argv[1]) == "-g") {
        useRandomGeneration = true;
        if (argc < 4) {
            std::cerr << "Error: --generate requires N and d arguments" << std::endl;
            std::cerr << "Usage: " << argv[0] << " --generate <N> <d> [query_index]" << std::endl;
            return 1;
        }
        try {
            auto result = parseN(argv[2]);
            N = result.first;
            n = result.second;
            N_is_power_of_2 = (n > 0);
        } catch (const std::exception& e) {
            std::cerr << "Error parsing N: " << e.what() << std::endl;
            return 1;
        }
        d_value = std::stoull(argv[3]);
        queryIndex = (argc > 4) ? std::stoull(argv[4]) : 0;
    } else {
        dataFile = argv[1];
        queryIndex = (argc > 2) ? std::stoull(argv[2]) : 0;
        columnName = (argc > 3) ? argv[3] : "";
    }
    
    std::cout << "========================================" << std::endl;
    if (useRandomGeneration) {
        std::cout << "  VLHEPIR with Random Database" << std::endl;
        std::cout << "========================================" << std::endl;
        std::cout << "Mode: Random generation" << std::endl;
        std::cout << "Number of elements (N): " << N;
        if (N_is_power_of_2) {
            std::cout << " = 2^" << n;
        }
        std::cout << std::endl;
        std::cout << "Precision (d): " << d_value << " bits" << std::endl;
        std::cout << "Query index: " << queryIndex << std::endl;
    } else {
        // Detect file format
        FileFormat format = detectFileFormat(dataFile);
        
        if (format == FileFormat::UNKNOWN) {
            std::cerr << "Error: unrecognized file format. Supported formats: .csv, .parquet" << std::endl;
            return 1;
        }
        
        std::cout << "  VLHEPIR with " << (format == FileFormat::PARQUET ? "Parquet" : "CSV") << std::endl;
        std::cout << "========================================" << std::endl;
        std::cout << "File: " << dataFile << std::endl;
        std::cout << "Format: " << (format == FileFormat::PARQUET ? "Parquet" : "CSV") << std::endl;
        std::cout << "Precision (d): " << d << " bits" << std::endl;
        std::cout << "Query index: " << queryIndex << std::endl;
        if (!columnName.empty()) {
            std::cout << "Column: " << columnName << std::endl;
        }
    }
    std::cout << std::endl;
    
    // ========================================================================
    // 2. Analyze the file or generate random data
    // ========================================================================
    VLHEPIR pir = [&]() -> VLHEPIR {
        if (useRandomGeneration) {
            std::cout << "=== Random Database Generation ===" << std::endl;
            std::cout << "Generating a random database of " << N << " elements..." << std::endl;
            std::cout << "Values in [0, " << ((1ULL << d_value) - 1) << "]" << std::endl;
            std::cout << std::endl;
            
            // ========================================================================
            // 3. Create PIR from random data
            // ========================================================================
            std::cout << "=== Parameters instantiation ===" << std::endl;
            return createVLHEPIRFromRandomData(
                N,          // number of elements
                d_value,     // precision in bits
                true,        // allowTrivial
                false,       // verbose (set to true to see detailed optimization)
                false,       // simplePIR
                1,           // batchSize
                false        // honestHint
            );
        } else {
            std::cout << "=== File Analysis ===" << std::endl;
            FileFormat format = detectFileFormat(dataFile);
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
            return createVLHEPIRFromFile(
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
        }
    }();
    std::cout << "Database size: " << (N * d) / (8.0 * (1ULL << 20)) << " MiB" << std::endl;
    std::cout << "Database parameters: ";
    pir.dbParams.print();
    std::cout << std::endl;
    
    // ========================================================================
    // 4. Prepare database for queries
    // ========================================================================
    std::cout << "=== Database Preparation ===" << std::endl;
    
    Matrix D;
    PackedMatrix D_packed = [&]() -> PackedMatrix {
        if (useRandomGeneration) {
            // For random generation, create the packed matrix directly
            // as in pir_bench.cpp to avoid allocating the complete Database
            std::cout << "Creating packed matrix directly (like in benchmark)..." << std::endl;
            PackedMatrix packed = packMatrixHardCoded(pir.dbParams.ell, pir.dbParams.m, pir.dbParams.p, true);
            std::cout << "Matrix D packed (dimensions: " 
                      << packed.mat.rows << " x " << packed.mat.cols << ")" << std::endl;
            
            // For GenerateHint, we need the unpacked matrix D
            // We can create a dummy matrix D or use GenerateFakeHint
            // For now, let's create a dummy matrix D (will be used only for H)
            D = Matrix(pir.dbParams.ell, pir.dbParams.m);
            random_fast(D, pir.dbParams.p);
            std::cout << "Database matrix D created (dimensions: " 
                      << D.rows << " x " << D.cols << ")" << std::endl;
            
            return packed;
        } else {
            // For files, use the normal method
            D = pir.db.packDataInMatrix(pir.dbParams, true);
            std::cout << "Database packed into matrix D (dimensions: " 
                      << D.rows << " x " << D.cols << ")" << std::endl;
            
            PackedMatrix packed = packMatrixHardCoded(D, pir.lhe.p);
            std::cout << "Matrix D packed (dimensions: " 
                      << packed.mat.rows << " x " << packed.mat.cols << ")" << std::endl;
            
            return packed;
        }
    }();
    std::cout << std::endl;
    
    // ========================================================================
    // 5. Offline Phase (can be done once)
    // ========================================================================
    std::cout << "=== Offline Phase ===" << std::endl;
    
    // Generate public matrix A
    Matrix A = pir.Init();
    std::cout << "Public matrix A generated" << std::endl;
    
    // Generate hint H with the real matrix D (needed for Recover)
    // Matrix H = pir.GenerateHint(A, D);
    Matrix H = pir.GenerateFakeHint();
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
    // For random generation, we cannot retrieve the expected value
    // because the complete Database is not allocated
    entry_t expectedValue;
    bool canVerify = false;
    if (!useRandomGeneration && pir.db.alloc) {
        expectedValue = pir.db.getDataAtIndex(queryIndex);
        canVerify = true;
        std::cout << "Expected value at index " << queryIndex << ": ";
        printEntry(expectedValue);
        std::cout << std::endl;
    } else {
        std::cout << "Note: Cannot verify expected value (random generation mode)" << std::endl;
    }
    
    // Generate query
    auto ct_sk = pir.Query(A, queryIndex);
    Matrix ct = std::get<0>(ct_sk);  // Ciphertext (encrypted query)
    Matrix sk = std::get<1>(ct_sk);  // Secret key (for decryption)
    
    // Measure over multiple iterations (as in the benchmark)
    uint64_t iters = 10;
    auto start_time = std::chrono::high_resolution_clock::now();
    for (uint64_t i = 0; i < iters; i++) {
        pir.Query(A, queryIndex);
    }
    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    double avg_time = duration.count() / double(iters);
    std::cout << "Query generation time: " << avg_time << " ms" << std::endl;
    
    std::cout << "Query generated for index " << queryIndex << std::endl;
    std::cout << "Query size: " 
              << ct.rows * ct.cols * sizeof(Elem) / 1024.0 
              << " KiB" << std::endl;
    std::cout << std::endl;
    
    // ========================================================================
    // 7. Online Phase - Generate answer (server side)
    // ========================================================================
    std::cout << "=== Online Phase - Answer ===" << std::endl;
    
    // First execution (warmup, not measured)
    Matrix ans = pir.Answer(ct, D_packed);
    
    // Measure over multiple iterations (as in the benchmark)
    iters = 10;
    start_time = std::chrono::high_resolution_clock::now();
    for (uint64_t i = 0; i < iters; i++) {
        ans = pir.Answer(ct, D_packed);
    }
    end_time = std::chrono::high_resolution_clock::now();
    duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    avg_time = duration.count() / double(iters);
    std::cout << "Answer generation time: " << avg_time << " ms" << std::endl;
    std::cout << "Answer generated" << std::endl;
    std::cout << "Answer size: " 
              << ans.rows * ans.cols * sizeof(Elem) / 1024.0 
              << " KiB" << std::endl;
    std::cout << std::endl;
    

    if (verify) {
    // ========================================================================
    // 8. Online Phase - Verification
    // ========================================================================
    std::cout << "=== Online Phase - Verification ===" << std::endl;
    
    // Generate proof Z for real
    Matrix Z = pir.Prove(hash, ct, ans, D_packed);
    
    // Measure over multiple iterations (as in the benchmark)
    iters = 1;  // Proof is more expensive, we measure over 1 iteration
    start_time = std::chrono::high_resolution_clock::now();
    for (uint64_t i = 0; i < iters; i++) {
        Z = pir.Prove(hash, ct, ans, D_packed);
    }
    end_time = std::chrono::high_resolution_clock::now();
    duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    avg_time = duration.count() / double(iters);
    std::cout << "Proof generation time: " << avg_time << " ms" << std::endl;
    
    // Verify proof with Verify (real verification with fake=false)
    pir.Verify(A, H, hash, ct, ans, Z, false);
    std::cout << "Verification successful ✓" << std::endl;
    std::cout << std::endl;
    }
    // ========================================================================
    // 9. Online Phase - Result recovery (client side)
    // ========================================================================
    std::cout << "=== Online Phase - Recovery ===" << std::endl;
    entry_t result = pir.Recover(H, ans, sk, queryIndex);
    
    // Measure over multiple iterations (as in the benchmark)
    iters = 10;
    start_time = std::chrono::high_resolution_clock::now();
    for (uint64_t i = 0; i < iters; i++) {
        result = pir.Recover(H, ans, sk, queryIndex);
    }
    end_time = std::chrono::high_resolution_clock::now();
    duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    avg_time = duration.count() / double(iters);
    std::cout << "Recovery time: " << avg_time << " ms" << std::endl;
    std::cout << "Recovered result: ";
    printEntry(result);
    std::cout << std::endl;
    
    // ========================================================================
    // 10. Verification
    // ========================================================================
    std::cout << std::endl;
    std::cout << "=== Verification ===" << std::endl;
    if (canVerify) {
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
    } else {
        std::cout << "✓ Recovery completed. Value recovered: ";
        printEntry(result);
        std::cout << " (verification skipped in random generation mode)" << std::endl;
    }
    
    std::cout << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "  PIR query completed successfully!" << std::endl;
    std::cout << "========================================" << std::endl;
    
    return 0;
}