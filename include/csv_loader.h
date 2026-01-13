#ifndef CSV_LOADER_H
#define CSV_LOADER_H

#include "pir/database.h"
#include "pir/pir.h"
#include <string>
#include <cstdint>

// ============================================================================
// Fonctions utilitaires
// ============================================================================

/**
 * Compte le nombre de lignes dans un fichier CSV (sans l'en-tête)
 */
uint64_t countCSVLines(const std::string& csvFilePath, bool hasHeader = true);

/**
 * Détermine la taille en bits nécessaire pour stocker une valeur
 */
uint64_t calculateBitSize(uint64_t value);

/**
 * Vérifie que toutes les valeurs dans la colonne sont valides pour d bits
 * Si d=1, vérifie que les valeurs sont 0 ou 1
 * Sinon, vérifie que les valeurs sont dans [0, 2^d-1]
 */
bool validateColumnForD(const std::string& csvFilePath, 
                        uint64_t d,
                        bool hasHeader = true);

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
                         bool hasHeader = true,
                         uint64_t maxRows = 0);

// ============================================================================
// Fonctions de création de PIR depuis CSV
// ============================================================================

/**
 * Crée un VLHEPIR à partir d'un fichier CSV
 * Détermine automatiquement N, d doit être spécifié
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
 * Affiche des statistiques sur un fichier CSV
 */
void printCSVStats(const std::string& csvFilePath,
                   uint64_t d,
                   bool hasHeader = true);

#endif // CSV_LOADER_H