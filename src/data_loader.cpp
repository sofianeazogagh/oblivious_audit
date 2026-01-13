#include "data_loader.h"
#include "pir/database.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>

// Support Parquet avec Apache Arrow
#ifdef PARQUET_SUPPORT
#include <arrow/api.h>
#include <arrow/io/api.h>
#include <parquet/arrow/reader.h>
#include <parquet/arrow/writer.h>
#include <parquet/stream_reader.h>
#include <parquet/stream_writer.h>
#endif

// ============================================================================
// Fonctions utilitaires
// ============================================================================

/**
 * Compte le nombre de lignes dans un fichier CSV (sans l'en-tête)
 */
uint64_t countCSVLines(const std::string& csvFilePath, bool hasHeader) {
    std::ifstream file(csvFilePath);
    if (!file.is_open()) {
        std::cerr << "Erreur: impossible d'ouvrir le fichier " << csvFilePath << std::endl;
        return 0;
    }
    
    std::string line;
    uint64_t count = 0;
    
    // Ignorer l'en-tête si présent
    if (hasHeader && std::getline(file, line)) {
        // Ligne d'en-tête ignorée
    }
    
    // Compter les lignes de données
    while (std::getline(file, line)) {
        // Ignorer les lignes vides
        if (!line.empty() && line.find_first_not_of(" \t\r\n") != std::string::npos) {
            count++;
        }
    }
    
    file.close();
    return count;
}

/**
 * Détermine la taille en bits nécessaire pour stocker une valeur
 */
uint64_t calculateBitSize(uint64_t value) {
    if (value == 0) return 1;
    return static_cast<uint64_t>(std::floor(std::log2(value)) + 1);
}

/**
 * Vérifie que toutes les valeurs dans la colonne sont valides pour d bits
 * Si d=1, vérifie que les valeurs sont 0 ou 1
 * Sinon, vérifie que les valeurs sont dans [0, 2^d-1]
 */
bool validateColumnForD(const std::string& csvFilePath, 
                        uint64_t d,
                        bool hasHeader) {
    std::ifstream file(csvFilePath);
    if (!file.is_open()) {
        std::cerr << "Erreur: impossible d'ouvrir le fichier " << csvFilePath << std::endl;
        return false;
    }
    
    std::string line;
    uint64_t rowCount = 0;
    entry_t maxValue = (entry_t(1) << d) - entry_t(1);
    
    // Ignorer l'en-tête
    if (hasHeader && std::getline(file, line)) {
        // Ligne d'en-tête ignorée
    }
    
    while (std::getline(file, line)) {
        if (line.empty()) continue;
        
        std::stringstream ss(line);
        std::string cell;
        
        // Prendre la première colonne
        if (std::getline(ss, cell, ',')) {
            // Enlever les espaces
            cell.erase(0, cell.find_first_not_of(" \t\r\n"));
            cell.erase(cell.find_last_not_of(" \t\r\n") + 1);
            
            if (!cell.empty()) {
                try {
                    uint64_t value = std::stoull(cell);
                    entry_t entryValue = entry_t(static_cast<unsigned long>(value));
                    
                    if (entryValue > maxValue) {
                        std::cerr << "Erreur: valeur trop grande trouvée à la ligne " 
                                  << (rowCount + 1) << ": " << cell 
                                  << " (max pour d=" << d << ": " << maxValue.toUnsignedLong() << ")" << std::endl;
                        file.close();
                        return false;
                    }
                } catch (const std::exception& e) {
                    std::cerr << "Erreur: valeur non numérique à la ligne " 
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
// Fonctions de chargement dans Database
// ============================================================================

/**
 * Charge les données d'une colonne CSV dans une Database
 * Les valeurs doivent être dans [0, 2^d-1]
 */
bool loadDatabaseFromCSV(Database& db,
                         const std::string& csvFilePath,
                         uint64_t d,
                         bool hasHeader,
                         uint64_t maxRows) {
    // Allouer la mémoire si nécessaire
    if (!db.alloc) {
        db.data = (entry_t*)malloc(db.N * sizeof(entry_t));
        db.alloc = true;
    }
    
    if (!db.data) {
        std::cerr << "Erreur: échec d'allocation mémoire" << std::endl;
        return false;
    }
    
    memset(db.data, 0, db.N * sizeof(entry_t));
    
    std::ifstream file(csvFilePath);
    if (!file.is_open()) {
        std::cerr << "Erreur: impossible d'ouvrir le fichier " << csvFilePath << std::endl;
        return false;
    }
    
    std::string line;
    uint64_t index = 0;
    entry_t modulus = entry_t(1) << d;
    entry_t maxValue = modulus - entry_t(1);
    
    // Ignorer l'en-tête si présent
    if (hasHeader && std::getline(file, line)) {
        // Ligne d'en-tête ignorée
    }
    
    // Charger les données (première colonne uniquement)
    while (std::getline(file, line) && index < db.N && (maxRows == 0 || index < maxRows)) {
        if (line.empty()) continue;
        
        std::stringstream ss(line);
        std::string cell;
        
        // Prendre la première colonne
        if (std::getline(ss, cell, ',') && !cell.empty()) {
            try {
                // Enlever les espaces
                cell.erase(0, cell.find_first_not_of(" \t\r\n"));
                cell.erase(cell.find_last_not_of(" \t\r\n") + 1);
                
                if (!cell.empty()) {
                    uint64_t value = std::stoull(cell);
                    entry_t entryValue = entry_t(static_cast<unsigned long>(value));
                    
                    if (entryValue > maxValue) {
                        std::cerr << "Avertissement: valeur trop grande à la ligne " 
                                  << (index + 1) << ": " << cell 
                                  << " (max pour d=" << d << ": " << maxValue.toUnsignedLong() 
                                  << ", utilisé " << maxValue.toUnsignedLong() << ")" << std::endl;
                        db.data[index] = maxValue;
                    } else {
                        db.data[index] = entryValue % modulus;
                    }
                } else {
                    // Valeur vide, mettre à 0
                    db.data[index] = entry_t(0);
                }
            } catch (const std::exception& e) {
                // Si ce n'est pas un nombre, utiliser 0
                std::cerr << "Avertissement: valeur non numérique à la ligne " 
                          << (index + 1) << ": " << cell << " (utilisé 0)" << std::endl;
                db.data[index] = entry_t(0);
            }
        } else {
            // Pas de colonne trouvée, mettre à 0
            db.data[index] = entry_t(0);
        }
        
        index++;
    }
    
    file.close();
    
    if (index < db.N) {
        std::cerr << "Avertissement: seulement " << index 
                  << " lignes chargées sur " << db.N << " attendues" << std::endl;
    }
    
    return true;
}

// ============================================================================
// Fonctions de création de PIR depuis CSV
// ============================================================================

/**
 * Crée un VeriSimplePIR à partir d'un fichier CSV
 * Détermine automatiquement N, d doit être spécifié
 */
VLHEPIR createVLHEPIRFromCSV(const std::string& csvFilePath,
                             uint64_t d,
                             bool hasHeader,
                             bool allowTrivial,
                             bool verbose,
                             bool simplePIR,
                             uint64_t batchSize,
                             bool honestHint) {
    // 1. Compter le nombre de lignes
    uint64_t N = countCSVLines(csvFilePath, hasHeader);
    if (N == 0) {
        std::cerr << "Erreur: aucune donnée trouvée dans le CSV" << std::endl;
        exit(1);
    }
    
    // 2. Vérifier que toutes les valeurs sont valides pour d bits
    if (!validateColumnForD(csvFilePath, d, hasHeader)) {
        entry_t maxValue = (entry_t(1) << d) - entry_t(1);
        std::cerr << "Erreur: le CSV doit contenir uniquement des valeurs dans [0, " 
                  << maxValue.toUnsignedLong() << "] pour d=" << d << std::endl;
        exit(1);
    }
    
    if (verbose) {
        std::cout << "CSV Analysis:" << std::endl;
        std::cout << "  Nombre d'éléments (N): " << N << std::endl;
        std::cout << "  Taille en bits (d): " << d << std::endl;
        std::cout << "  Taille de la base: " << (N * d) / (8.0 * (1ULL << 20)) << " MiB" << std::endl;
    }
    
    // 3. Créer la base de données
    Database db(N, d);
    
    // 4. Charger les données depuis le CSV
    if (!loadDatabaseFromCSV(db, csvFilePath, d, hasHeader)) {
        std::cerr << "Erreur: échec du chargement du CSV" << std::endl;
        exit(1);
    }
    
    // 5. Créer le PIR
    VLHEPIR pir(
        N, d,
        allowTrivial,
        verbose,
        simplePIR,
        false,      // randomData = false (on charge depuis CSV)
        batchSize,
        honestHint
    );
    
    // 6. Copier les données dans pir.db
    // Note: pir.db est déjà créé dans le constructeur, on doit copier les données
    if (pir.db.alloc) {
        free(pir.db.data);
    }
    pir.db.data = (entry_t*)malloc(N * sizeof(entry_t));
    pir.db.alloc = true;
    memcpy(pir.db.data, db.data, N * sizeof(entry_t));
    
    return pir;
}
/**
 * Affiche des statistiques sur un fichier CSV
 */
void printCSVStats(const std::string& csvFilePath,
                   uint64_t d,
                   bool hasHeader) {
    uint64_t N = countCSVLines(csvFilePath, hasHeader);
    entry_t maxValue = (entry_t(1) << d) - entry_t(1);
    
    // Trouver min et max
    uint64_t minVal = UINT64_MAX, maxVal = 0;
    std::ifstream file(csvFilePath);
    if (file.is_open()) {
        std::string line;
        if (hasHeader && std::getline(file, line)) {
            // Ignorer l'en-tête
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
                        // Ignorer
                    }
                }
            }
        }
        file.close();
    }
    
    std::cout << "=== Statistiques CSV ===" << std::endl;
    std::cout << "Fichier: " << csvFilePath << std::endl;
    std::cout << "Nombre de lignes (N): " << N << std::endl;
    std::cout << "Taille en bits (d): " << d << std::endl;
    std::cout << "Valeur maximale autorisée: " << maxValue.toUnsignedLong() << std::endl;
    if (minVal != UINT64_MAX) {
        std::cout << "Valeur minimale trouvée: " << minVal << std::endl;
        std::cout << "Valeur maximale trouvée: " << maxVal << std::endl;
    }
    std::cout << "Taille de la base: " << (N * d) / (8.0 * (1ULL << 20)) << " MiB" << std::endl;
    std::cout << "===============================" << std::endl;
}

// ============================================================================
// Fonctions pour fichiers Parquet
// ============================================================================

FileFormat detectFileFormat(const std::string& filePath) {
    // Extraire l'extension du fichier
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
        std::shared_ptr<arrow::io::ReadableFile> infile;
        arrow::Status status = arrow::io::ReadableFile::Open(parquetFilePath, &infile);
        if (!status.ok()) {
            std::cerr << "Erreur: impossible d'ouvrir le fichier Parquet " << parquetFilePath << std::endl;
            return 0;
        }

        std::unique_ptr<parquet::arrow::FileReader> reader;
        status = parquet::arrow::OpenFile(infile, arrow::default_memory_pool(), &reader);
        if (!status.ok()) {
            std::cerr << "Erreur: impossible de lire le fichier Parquet" << std::endl;
            return 0;
        }

        std::shared_ptr<arrow::Table> table;
        status = reader->ReadTable(&table);
        if (!status.ok()) {
            std::cerr << "Erreur: impossible de lire la table Parquet" << std::endl;
            return 0;
        }

        return table->num_rows();
    } catch (const std::exception& e) {
        std::cerr << "Erreur lors de la lecture du fichier Parquet: " << e.what() << std::endl;
        return 0;
    }
}

bool validateParquetColumnForD(const std::string& parquetFilePath,
                               uint64_t d,
                               const std::string& columnName) {
    try {
        std::shared_ptr<arrow::io::ReadableFile> infile;
        arrow::Status status = arrow::io::ReadableFile::Open(parquetFilePath, &infile);
        if (!status.ok()) {
            std::cerr << "Erreur: impossible d'ouvrir le fichier Parquet" << std::endl;
            return false;
        }

        std::unique_ptr<parquet::arrow::FileReader> reader;
        status = parquet::arrow::OpenFile(infile, arrow::default_memory_pool(), &reader);
        if (!status.ok()) {
            return false;
        }

        std::shared_ptr<arrow::Table> table;
        status = reader->ReadTable(&table);
        if (!status.ok()) {
            return false;
        }

        std::string colName = columnName.empty() ? table->ColumnName(0) : columnName;
        std::shared_ptr<arrow::ChunkedArray> column = table->GetColumnByName(colName);
        if (!column) {
            std::cerr << "Erreur: colonne '" << colName << "' introuvable" << std::endl;
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
                        std::cerr << "Erreur: valeur invalide trouvée: " << value << std::endl;
                        return false;
                    }
                }
            } else if (chunk->type_id() == arrow::Type::UINT64) {
                auto uint64_array = std::static_pointer_cast<arrow::UInt64Array>(chunk);
                for (int64_t i = 0; i < uint64_array->length(); i++) {
                    if (uint64_array->IsNull(i)) continue;
                    uint64_t value = uint64_array->Value(i);
                    if (entry_t(static_cast<unsigned long>(value)) > maxValue) {
                        std::cerr << "Erreur: valeur invalide trouvée: " << value << std::endl;
                        return false;
                    }
                }
            } else {
                std::cerr << "Erreur: type de colonne non supporté (doit être INT64 ou UINT64)" << std::endl;
                return false;
            }
        }

        return true;
    } catch (const std::exception& e) {
        std::cerr << "Erreur lors de la validation Parquet: " << e.what() << std::endl;
        return false;
    }
}

bool loadDatabaseFromParquet(Database& db,
                            const std::string& parquetFilePath,
                            uint64_t d,
                            const std::string& columnName,
                            uint64_t maxRows) {
    try {
        std::shared_ptr<arrow::io::ReadableFile> infile;
        arrow::Status status = arrow::io::ReadableFile::Open(parquetFilePath, &infile);
        if (!status.ok()) {
            std::cerr << "Erreur: impossible d'ouvrir le fichier Parquet" << std::endl;
            return false;
        }

        std::unique_ptr<parquet::arrow::FileReader> reader;
        status = parquet::arrow::OpenFile(infile, arrow::default_memory_pool(), &reader);
        if (!status.ok()) {
            return false;
        }

        std::shared_ptr<arrow::Table> table;
        status = reader->ReadTable(&table);
        if (!status.ok()) {
            return false;
        }

        std::string colName = columnName.empty() ? table->ColumnName(0) : columnName;
        std::shared_ptr<arrow::ChunkedArray> column = table->GetColumnByName(colName);
        if (!column) {
            std::cerr << "Erreur: colonne '" << colName << "' introuvable" << std::endl;
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
        std::cerr << "Erreur lors du chargement Parquet: " << e.what() << std::endl;
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
        std::cerr << "Erreur: aucune donnée trouvée dans le fichier Parquet" << std::endl;
        exit(1);
    }

    if (!validateParquetColumnForD(parquetFilePath, d, columnName)) {
        entry_t maxValue = (entry_t(1) << d) - entry_t(1);
        std::cerr << "Erreur: le fichier Parquet doit contenir uniquement des valeurs dans [0, " 
                  << maxValue.toUnsignedLong() << "] pour d=" << d << std::endl;
        exit(1);
    }

    if (verbose) {
        std::cout << "Parquet Analysis:" << std::endl;
        std::cout << "  Nombre d'éléments (N): " << N << std::endl;
        std::cout << "  Taille en bits (d): " << d << std::endl;
        std::cout << "  Taille de la base: " << (N * d) / (8.0 * (1ULL << 20)) << " MiB" << std::endl;
    }

    Database db(N, d);
    if (!loadDatabaseFromParquet(db, parquetFilePath, d, columnName)) {
        std::cerr << "Erreur: échec du chargement du fichier Parquet" << std::endl;
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
        std::shared_ptr<arrow::io::ReadableFile> infile;
        arrow::Status status = arrow::io::ReadableFile::Open(parquetFilePath, &infile);
        if (!status.ok()) {
            std::cerr << "Erreur: impossible d'ouvrir le fichier Parquet" << std::endl;
            return;
        }

        std::unique_ptr<parquet::arrow::FileReader> reader;
        status = parquet::arrow::OpenFile(infile, arrow::default_memory_pool(), &reader);
        if (!status.ok()) {
            return;
        }

        std::shared_ptr<arrow::Table> table;
        status = reader->ReadTable(&table);
        if (!status.ok()) {
            return;
        }

        std::string colName = columnName.empty() ? table->ColumnName(0) : columnName;
        std::shared_ptr<arrow::ChunkedArray> column = table->GetColumnByName(colName);
        if (!column) {
            std::cerr << "Erreur: colonne '" << colName << "' introuvable" << std::endl;
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

        std::cout << "=== Statistiques Parquet ===" << std::endl;
        std::cout << "Fichier: " << parquetFilePath << std::endl;
        std::cout << "Colonne: " << colName << std::endl;
        std::cout << "Nombre de lignes (N): " << N << std::endl;
        std::cout << "Taille en bits (d): " << d << std::endl;
        std::cout << "Valeur maximale autorisée: " << maxValue.toUnsignedLong() << std::endl;
        if (minVal != UINT64_MAX) {
            std::cout << "Valeur minimale trouvée: " << minVal << std::endl;
            std::cout << "Valeur maximale trouvée: " << maxVal << std::endl;
        }
        std::cout << "Taille de la base: " << (N * d) / (8.0 * (1ULL << 20)) << " MiB" << std::endl;
        std::cout << "===============================" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Erreur lors de l'analyse Parquet: " << e.what() << std::endl;
    }
}

#else

// Stubs lorsque Parquet n'est pas disponible
uint64_t countParquetLines(const std::string&, const std::string&) {
    std::cerr << "Erreur: support Parquet non compilé. Installez Apache Arrow C++ et recompilez avec -DPARQUET_SUPPORT" << std::endl;
    return 0;
}

bool validateParquetColumnForD(const std::string&, uint64_t, const std::string&) {
    std::cerr << "Erreur: support Parquet non compilé" << std::endl;
    return false;
}

bool loadDatabaseFromParquet(Database&, const std::string&, uint64_t, const std::string&, uint64_t) {
    std::cerr << "Erreur: support Parquet non compilé" << std::endl;
    return false;
}

VLHEPIR createVLHEPIRFromParquet(const std::string&, uint64_t, const std::string&, bool, bool, bool, uint64_t, bool) {
    std::cerr << "Erreur: support Parquet non compilé" << std::endl;
    exit(1);
}

void printParquetStats(const std::string&, uint64_t, const std::string&) {
    std::cerr << "Erreur: support Parquet non compilé" << std::endl;
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
            std::cerr << "Erreur: format de fichier non reconnu. Formats supportés: .csv, .parquet" << std::endl;
            exit(1);
    }
}