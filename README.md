# Oblivious Audit

Project using VeriSimplePIR to perform PIR (Private Information Retrieval) queries on CSV or Parquet files. This project allows retrieving database elements privately without revealing which element is being accessed.

## Prerequisites

### Operating System
- macOS or Linux (Ubuntu 20.04+ recommended)

### Required Dependencies
- `clang++` (version 10.0.0 or higher)
- `make`
- OpenSSL (for SHA)
- `pkg-config` (to detect dependencies)

### Installing Dependencies

**On Ubuntu/Debian:**
```bash
sudo apt install make clang++ libssl-dev pkg-config
```

**On macOS:**
```bash
brew install openssl pkg-config
```

### Optional Dependencies
- **Apache Arrow/Parquet**: For Parquet file support
  - On Ubuntu: `sudo apt install libarrow-dev libparquet-dev`
  - On macOS: `brew install apache-arrow`

## Installation

### Build the Project

Simply run `make` in the project root directory. The Makefile will automatically build VeriSimplePIR if it hasn't been built yet, then compile the main project:

```bash
make
```

The executable will be created at `bin/pir`.

**Note:** The first time you run `make`, it will automatically compile VeriSimplePIR (which may take a few minutes). Subsequent builds will be faster as VeriSimplePIR will only be rebuilt if needed.

### Optional: Build VeriSimplePIR Separately

If you want to build VeriSimplePIR separately:

```bash
make verisimplepir
```

The compiled library will be located at `VeriSimplePIR/bin/lib/libverisimplepir.dylib` (macOS) or `VeriSimplePIR/bin/lib/libverisimplepir.so` (Linux).

### Optional Configuration

If you want to disable Parquet support (enabled by default if available):

```bash
make PARQUET_SUPPORT=0
```

## Usage

### General Syntax

```bash
./bin/pir <data_file> [query_index] [column_name]
```

or to generate a random database:

```bash
./bin/pir --generate <N> <d> [query_index]
```

### Parameters

- **`<data_file>`**: Path to a CSV or Parquet file containing a column of numeric values
- **`<N>`**: Number of elements in the database (can be a number like `1024` or a power of 2 like `2^10` or `2**20`)
- **`<d>`**: Number of bits per element (values in `[0, 2^d-1]`)
- **`[query_index]`**: Index of the element to retrieve (default: 0)
- **`[column_name]`**: Column name (optional, for Parquet files only)

### Examples

#### 1. Query on a CSV File

```bash
./bin/pir data/test.csv 5
```

Retrieves the element at index 5 from the `data/test.csv` file.

#### 2. Query on a Parquet File

```bash
./bin/pir data/database.parquet 0 value
```

Retrieves the element at index 0 from the `value` column in the Parquet file.

#### 3. Generate a Random Database

```bash
./bin/pir --generate 1000 1 5
```

Generates a random database of 1000 elements with 1 bit per element (values 0 or 1) and retrieves the element at index 5.

#### 4. Generation with Power of 2

```bash
./bin/pir --generate 2^10 8 42
```

or

```bash
./bin/pir --generate 2**20 8 42
```

Generates a database of 2^10 = 1024 elements (or 2^20 = 1048576) with 8 bits per element and retrieves the element at index 42.

### CSV File Format

CSV files must contain a column of numeric values. By default, the program assumes the first line is a header.

Example CSV file (`data/test.csv`):
```csv
id,value,description
0,42,first_element
1,100,second_element
2,255,third_element
```

The program will automatically use the first numeric column found.

### Parquet File Format

For Parquet files, you must specify the column name to use:

```bash
./bin/pir data/database.parquet 0 column_name
```

## How It Works

The program performs the following steps:

1. **File Analysis**: Detects the format (CSV or Parquet) and analyzes the data
2. **Parameter Instantiation**: Configures PIR parameters based on the database size
3. **Database Preparation**: Prepares the data matrix for queries
4. **Offline Phase**: Generates the public matrix A and hint H
5. **Online Phase - Query**: Generates the encrypted query for the requested index
6. **Online Phase - Answer**: Generates the server response
7. **Online Phase - Recovery**: Decrypts the response to obtain the requested value
8. **Verification**: Compares the recovered value with the expected value (if available)

## Cleaning

To remove compiled files from the main project:

```bash
make clean
```

To also clean VeriSimplePIR build files:

```bash
make clean-all
```

## Troubleshooting

### Error: "VeriSimplePIR library not found"

The Makefile should automatically build VeriSimplePIR. If you encounter this error, try:

```bash
make verisimplepir
```

If the error persists, ensure the VeriSimplePIR directory exists in the project root.

### Error: "unrecognized file format"

Check that your file has the `.csv` or `.parquet` extension and that the format is correct.

### Error: "index out of bounds"

The query index must be less than the number of elements in the database. Check your file size.

### Compilation Issues on macOS

If you encounter issues with library paths on macOS, the Makefile automatically configures paths using `install_name_tool`.

## Project Structure

```
oblivious_audit/
├── bin/                    # Compiled executables
│   └── pir
├── build/                  # Compilation object files
├── data/                   # Data files (CSV, Parquet)
├── include/                # Header files
│   └── data_loader.h
├── src/                    # Source code
│   ├── data_loader.cpp
│   └── main.cpp
├── VeriSimplePIR/          # VeriSimplePIR library
└── Makefile               # Build file
```

## References

- [VeriSimplePIR](https://github.com/ahenzinger/simplepir): PIR library used in this project
