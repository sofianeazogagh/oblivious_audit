# Oblivious Audit

Project using VeriSimplePIR to perform PIR (Private Information Retrieval) queries for an audit model purpose.

## Getting Started

### Cloning the Repository

To clone this repository with all submodules in one command:

```bash
git clone --recursive git@github.com:sofianeazogagh/oblivious_audit.git
cd oblivious_audit
```

Alternatively, if you've already cloned the repository without submodules:

```bash
git clone git@github.com:sofianeazogagh/oblivious_audit.git
cd oblivious_audit
git submodule update --init --recursive
```

**Note:** This project uses VeriSimplePIR as a git submodule. Make sure to initialize submodules before building the project.

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

<!-- ### Optional: Build VeriSimplePIR Separately

If you want to build VeriSimplePIR separately:

```bash
make verisimplepir
```

The compiled library will be located at `VeriSimplePIR/bin/lib/libverisimplepir.dylib` (macOS) or `VeriSimplePIR/bin/lib/libverisimplepir.so` (Linux). -->


## Usage

### General Syntax

```bash
./bin/pir <data_file> [query_index]
```

or to generate a random database (much faster):

```bash
./bin/pir --generate <N> <d> [query_index]
```

### Parameters

- **`<data_file>`**: Path to a CSV or Parquet file containing a column of numeric values
- **`<N>`**: Number of elements in the database (can be a number like `1024` or a power of 2 like `2^10` or `2**20`)
- **`<d>`**: Number of bits per element (values in `[0, 2^d-1]`)
- **`[query_index]`**: Index of the element to retrieve (default: 0)

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

## References

- [VeriSimplePIR](https://github.com/ahenzinger/simplepir): PIR library used in this project
