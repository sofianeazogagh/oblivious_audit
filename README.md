# Oblivious Audit — R²esPIR

Code for *Manipulation-Proof Oblivious Audits against Deceptive Model Providers*.

The repository holds the two halves of the experimental evaluation:

| Part | Directory | Paper artifacts |
| --- | --- | --- |
| PIR benchmarks, built on [VeriSimplePIR](https://github.com/sofianeazogagh/VeriSimplePIR) (C++) | `src/`, `include/`, `Makefile` | Table 1 |
| Audit simulations, bounds and figures (Python) | `main.py`, `respir/` | Tables 2 and 3, Figures 1 and 2 |

Every output is written to `generated/`.

## 1. Install

```bash
git clone --recursive https://github.com/sofianeazogagh/oblivious_audit.git
cd oblivious_audit
```

(If you cloned without `--recursive`: `git submodule update --init --recursive`.)

**PIR benchmarks (C++).** Requires `clang++` (≥ 10), `make`, OpenSSL and `pkg-config`;
Apache Arrow/Parquet is optional and only needed to run a query over a Parquet file.

```bash
sudo apt install make clang libssl-dev pkg-config    # Ubuntu/Debian
brew install openssl pkg-config                      # macOS

make          # builds VeriSimplePIR first (a few minutes), then bin/pir
```

Two caveats: `make` does not support paths containing spaces, and if Arrow is
installed but does not build, `make PARQUET_SUPPORT=0` drops the (optional)
Parquet reader.

**Simulations (Python).** Requires [uv](https://docs.astral.sh/uv/getting-started/installation/);
it installs the pinned dependencies (`uv.lock`) and the right Python version on
first use, so there is nothing else to set up. All commands below are run from
the repository root.

## 2. Reproduce the paper

### Table 1 — PIR cost

`bin/pir --generate <N> <d> <index>` builds a random database of `N` entries of
`d` bits and answers one query, reporting the query/answer/recovery times and
sizes averaged over 10 runs. The database sizes of Table 1 correspond to a
1-bit database (`d = 1`) of the following sizes:

```bash
./bin/pir --generate 2^20 1 0   # 128 KiB
./bin/pir --generate 2^30 1 0   # 128 MiB
./bin/pir --generate 2^33 1 0   #   1 GiB
./bin/pir --generate 2^35 1 0   #   4 GiB
./bin/pir --generate 2^36 1 0   #   8 GiB
```

The large sizes need a machine with enough RAM (~2× the database size). A query
can also be run against a real label file: `./bin/pir data/labels.csv 5` or
`./bin/pir data/labels.parquet 0 <column>`.

### Figure 1 — theoretical bounds

No data needed, runs in a few seconds:

```bash
uv run main.py plot-bounds
```

writes `generated/fig_manipulation_costs_{balanced,unbalanced}.pdf` (Figure 1a)
and `generated/fig_detection_probability_{balanced,unbalanced}.pdf` (Figure 1b).

### Tables 2, 3 and Figure 2 — real datasets

First download the datasets. COMPAS and Default of Credit Card Clients (CCD)
are fetched automatically; [HateDay](https://huggingface.co/datasets/manueltonneau/hateday)
is gated, so accept its conditions on the Hugging Face Hub and log in
(`uv run hf auth login`, or export `HF_TOKEN`) beforehand.

```bash
uv run main.py download-data
```

The HateDay texts are then embedded with
[distiluse-base-multilingual-cased-v2](https://huggingface.co/sentence-transformers/distiluse-base-multilingual-cased-v2);
this is by far the longest step (minutes on a GPU, up to an hour on CPU) and its
result is cached in `generated/hateday_with_embeddings.parquet`.

```bash
uv run main.py datasets-infos                # Table 2 -> generated/datasets_infos.csv
uv run main.py manipulation-simulation-table # Table 3 -> generated/summary_results.csv
uv run main.py plot-detection                # Figure 2 -> generated/fig_detection_vs_canaries.pdf
```

`datasets-infos` trains one gradient boosting classifier per (dataset,
protected attribute) pair and must be run first: the other two commands read its
output. It takes a few minutes, mostly on HateDay.

### Appendix

```bash
uv run main.py plot-appendix-scenarios       # bounds for the appendix audit sizes
uv run main.py simulation                    # empirical manipulation cost -> generated/dpg_manipulation.csv
```

Add `--tune-hparams` to `datasets-infos` or `simulation` to select the
classifier hyperparameters by randomized search instead of using the defaults.
`uv run main.py --help` documents every command.

## Reference

- [VeriSimplePIR](https://github.com/ahenzinger/simplepir) — de Castro and Lee, *VeriSimplePIR: Verifiability in SimplePIR at No Online Cost for Honest Servers*, USENIX Security 2024.
