#include "data_loader.h"
#include <openssl/sha.h>
#include <iostream>
#include <iomanip>

// Fonction pour afficher une valeur entry_t
void printEntry(const entry_t& val) {
    std::cout << val.toUnsignedLong();
}

int main(int argc, char* argv[]) {
    // ========================================================================
    // 1. Configuration
    // ========================================================================
    // Précision de la base de données en bits
    const uint64_t d = 2;
    
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <data_file> [query_index] [column_name]" << std::endl;
        std::cerr << "  data_file: chemin vers le fichier CSV ou Parquet contenant une colonne de valeurs numériques" << std::endl;
        std::cerr << "  query_index: index de l'élément à récupérer (défaut: 0)" << std::endl;
        std::cerr << "  column_name: nom de la colonne (optionnel, pour Parquet uniquement)" << std::endl;
        std::cerr << std::endl;
        std::cerr << "Note: La précision d=" << d << " bits est définie dans le code source." << std::endl;
        std::cerr << "      Valeurs valides: [0, " << ((1ULL << d) - 1) << "]" << std::endl;
        std::cerr << "      Formats supportés: .csv, .parquet" << std::endl;
        return 1;
    }
    
    const std::string dataFile = argv[1];
    const uint64_t queryIndex = (argc > 2) ? std::stoull(argv[2]) : 0;
    const std::string columnName = (argc > 3) ? argv[3] : "";
    
    // Détecter le format du fichier
    FileFormat format = detectFileFormat(dataFile);
    
    if (format == FileFormat::UNKNOWN) {
        std::cerr << "Erreur: format de fichier non reconnu. Formats supportés: .csv, .parquet" << std::endl;
        return 1;
    }
    
    std::cout << "========================================" << std::endl;
    std::cout << "  VLHEPIR avec " << (format == FileFormat::PARQUET ? "Parquet" : "CSV") << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "Fichier: " << dataFile << std::endl;
    std::cout << "Format: " << (format == FileFormat::PARQUET ? "Parquet" : "CSV") << std::endl;
    std::cout << "Précision (d): " << d << " bits" << std::endl;
    std::cout << "Index de requête: " << queryIndex << std::endl;
    if (!columnName.empty()) {
        std::cout << "Colonne: " << columnName << std::endl;
    }
    std::cout << std::endl;
    
    // ========================================================================
    // 2. Analyser le fichier
    // ========================================================================
    std::cout << "=== Analyse du fichier ===" << std::endl;
    if (format == FileFormat::PARQUET) {
        printParquetStats(dataFile, d, columnName);
    } else {
        printCSVStats(dataFile, d, true);
    }
    std::cout << std::endl;
    
    // ========================================================================
    // 3. Créer le PIR depuis le fichier
    // ========================================================================
    std::cout << "=== Parameters instantiation ===" << std::endl;
    VLHEPIR pir = createVLHEPIRFromFile(
        dataFile,
        d,          // précision en bits
        columnName, // nom de la colonne (pour Parquet)
        true,       // hasHeader (pour CSV)
        true,       // allowTrivial
        false,      // verbose (mettre à true pour voir l'optimisation détaillée)
        false,      // simplePIR
        1,          // batchSize
        false       // honestHint
    );
    
    std::cout << "Database parameters: ";
    pir.dbParams.print();
    std::cout << std::endl;
    
    // ========================================================================
    // 4. Préparer la base de données pour les requêtes
    // ========================================================================
    std::cout << "=== Préparation de la base de données ===" << std::endl;
    // D'abord, packer les données de la base dans une matrice
    Matrix D = pir.db.packDataInMatrix(pir.dbParams, true);
    std::cout << "Base de données packée en matrice D (dimensions: " 
              << D.rows << " x " << D.cols << ")" << std::endl;
    
    // Ensuite, packer la matrice pour l'optimisation
    PackedMatrix D_packed = packMatrixHardCoded(D, pir.lhe.p);
    std::cout << "Matrice D packée (dimensions: " 
              << D_packed.mat.rows << " x " << D_packed.mat.cols << ")" << std::endl;
    std::cout << std::endl;
    
    // ========================================================================
    // 5. Phase Offline (peut être faite une seule fois)
    // ========================================================================
    std::cout << "=== Phase Offline ===" << std::endl;
    
    // Générer la matrice publique A
    Matrix A = pir.Init();
    std::cout << "Matrice publique A générée" << std::endl;
    
    // Générer le hint H avec la vraie matrice D (nécessaire pour Recover)
    Matrix H = pir.GenerateHint(A, D);
    std::cout << "Hint H généré" << std::endl;
    std::cout << "Taille du hint: " 
              << H.rows * H.cols * sizeof(Elem) / (1ULL << 20) 
              << " MiB" << std::endl;
    
    // Hasher A et H pour la génération de la preuve (nécessaire pour la vérification)
    unsigned char hash[SHA256_DIGEST_LENGTH];
    pir.HashAandH(hash, A, H);
    
    std::cout << std::endl;
    
    // ========================================================================
    // 6. Phase Online - Générer la requête
    // ========================================================================
    std::cout << "=== Phase Online - Requête ===" << std::endl;
    
    if (queryIndex >= pir.N) {
        std::cerr << "Erreur: index " << queryIndex 
                  << " hors limites (max: " << (pir.N - 1) << ")" << std::endl;
        return 1;
    }
    
    // Valeur attendue (pour vérification)
    entry_t expectedValue = pir.db.getDataAtIndex(queryIndex);
    std::cout << "Valeur attendue à l'index " << queryIndex << ": ";
    printEntry(expectedValue);
    std::cout << std::endl;
    
    // Générer la requête
    
    auto ct_sk = pir.Query(A, queryIndex);
    Matrix ct = std::get<0>(ct_sk);  // Ciphertext (requête chiffrée)
    Matrix sk = std::get<1>(ct_sk);  // Secret key (pour déchiffrer)
    
    std::cout << "Requête générée pour l'index " << queryIndex << std::endl;
    std::cout << "Taille de la requête: " 
              << ct.rows * ct.cols * sizeof(Elem) / 1024.0 
              << " KiB" << std::endl;
    std::cout << std::endl;
    
    // ========================================================================
    // 7. Phase Online - Générer la réponse (côté serveur)
    // ========================================================================
    std::cout << "=== Phase Online - Réponse ===" << std::endl;
    
    Matrix ans = pir.Answer(ct, D_packed);
    std::cout << "Réponse générée" << std::endl;
    std::cout << "Taille de la réponse: " 
              << ans.rows * ans.cols * sizeof(Elem) / 1024.0 
              << " KiB" << std::endl;
    std::cout << std::endl;
    
    // ========================================================================
    // 8. Phase Online - Vérification
    // ========================================================================
    std::cout << "=== Phase Online - Vérification ===" << std::endl;
    
    // Générer la preuve Z de manière réelle
    Matrix Z = pir.Prove(hash, ct, ans, D_packed);
    
    // Vérifier la preuve avec Verify (vraie vérification avec fake=false)
    pir.Verify(A, H, hash, ct, ans, Z, false);
    std::cout << "Vérification réussie ✓" << std::endl;
    std::cout << std::endl;
    
    // ========================================================================
    // 9. Phase Online - Récupération du résultat (côté client)
    // ========================================================================
    std::cout << "=== Phase Online - Récupération ===" << std::endl;
    
    entry_t result = pir.Recover(H, ans, sk, queryIndex);
    
    std::cout << "Résultat récupéré: ";
    printEntry(result);
    std::cout << std::endl;
    
    // ========================================================================
    // 10. Vérification
    // ========================================================================
    std::cout << std::endl;
    std::cout << "=== Vérification ===" << std::endl;
    if (result == expectedValue) {
        std::cout << "✓ Succès! La valeur récupérée correspond à la valeur attendue." 
                  << std::endl;
    } else {
        std::cout << "✗ Erreur! Valeur attendue: ";
        printEntry(expectedValue);
        std::cout << ", Valeur récupérée: ";
        printEntry(result);
        std::cout << std::endl;
        return 1;
    }
    
    std::cout << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "  Requête PIR complétée avec succès!" << std::endl;
    std::cout << "========================================" << std::endl;
    
    return 0;
}