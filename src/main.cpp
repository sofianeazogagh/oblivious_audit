#include "csv_loader.h"
#include "pir/preproc_pir.h"
#include "pir/pir.h"
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
        std::cerr << "Usage: " << argv[0] << " <csv_file> [query_index]" << std::endl;
        std::cerr << "  csv_file: chemin vers le fichier CSV contenant une colonne de valeurs numériques" << std::endl;
        std::cerr << "  query_index: index de l'élément à récupérer (défaut: 0)" << std::endl;
        std::cerr << std::endl;
        std::cerr << "Note: La précision d=" << d << " bits est définie dans le code source." << std::endl;
        std::cerr << "      Valeurs valides: [0, " << ((1ULL << d) - 1) << "]" << std::endl;
        return 1;
    }
    
    const std::string csvFile = argv[1];
    const uint64_t queryIndex = (argc > 2) ? std::stoull(argv[2]) : 0;
    
    std::cout << "========================================" << std::endl;
    std::cout << "  VeriSimplePIR avec CSV" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "Fichier CSV: " << csvFile << std::endl;
    std::cout << "Précision (d): " << d << " bits" << std::endl;
    std::cout << "Index de requête: " << queryIndex << std::endl;
    std::cout << std::endl;
    
    // ========================================================================
    // 2. Analyser le CSV
    // ========================================================================
    std::cout << "=== Analyse du CSV ===" << std::endl;
    printCSVStats(csvFile, d, true);
    std::cout << std::endl;
    
    // ========================================================================
    // 3. Créer le PIR depuis le CSV
    // ========================================================================
    std::cout << "=== Parameters instantiation ===" << std::endl;
    VeriSimplePIR pir = createPIRFromCSV(
        csvFile,
        d,          // précision en bits
        true,       // hasHeader
        true,       // allowTrivial
        false,      // verbose (mettre à true pour voir l'optimisation détaillée)
        true,       // preprocessed
        false       // honestHint
    );
    
    std::cout << "Database parameters: ";
    pir.dbParams.print();
    std::cout << std::endl;
    
    // Créer une instance VLHEPIR pour la vérification (nécessaire pour générer Z et C)
    // car VeriSimplePIR n'a pas de fonction Prove pour la phase online
    VLHEPIR vlhe_pir(pir.N, pir.d, true, false, false, false, 1, false);
    // Synchroniser les paramètres avec VeriSimplePIR
    vlhe_pir.m = pir.m;
    vlhe_pir.ell = pir.ell;
    vlhe_pir.lhe = pir.lhe;
    vlhe_pir.dbParams = pir.dbParams;
    
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
    vlhe_pir.HashAandH(hash, A, H);
    
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
    // 8. Phase Online - Vérification (optionnelle pour VeriSimplePIR)
    // ========================================================================
    std::cout << "=== Phase Online - Vérification ===" << std::endl;
    
    // Générer la preuve Z et la matrice binaire C de manière réelle
    Matrix Z = vlhe_pir.Prove(hash, ct, ans, D_packed);
    BinaryMatrix C = vlhe_pir.HashToC(hash, ct, ans);
    
    // Utiliser PreVerify avec fake=false pour la vraie vérification
    pir.PreVerify(ct, ans, Z, C, false);
    std::cout << "Vérification réussie " << std::endl;
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