from pathlib import Path
from typing import Literal

import polars as pl
import typer

from respir.analyze import (
    manipulation_bounds,
    plot_detection_vs_canaries,
    table_dp_to_hide,
)
from respir.audit import run_respir_simulation
from respir.data import (
    dataset_info,
    get_compas,
    get_credit_card_clients,
    get_hateday,
)

app = typer.Typer(rich_markup_mode=None, no_args_is_help=True)

# Audit setup shared by `simulation` and `datasets-infos`, so that both commands
# describe the same splits.
CANDIDATES_SIZE = 0.45
AUDIT_CANDIDATES_RATIO = 0.5
DELTA = 0.05


@app.command()
def download_data() -> None:
    """Download and cache every dataset used by the experiments.

    Fetches the Default of Credit Card Clients (UCI), COMPAS and HateDay
    datasets into `data/`. Each dataset is only downloaded if its cached file is
    missing, so the command is safe to re-run. HateDay additionally requires
    sentence embeddings: if `generated/hateday_with_embeddings.parquet` does not
    exist, the texts are encoded with a sentence-transformers model on GPU,
    which is by far the longest step.
    """

    typer.echo("Downloading credit card default dataset...")
    get_credit_card_clients()

    typer.echo("Downloading COMPAS dataset...")
    get_compas()

    typer.echo("Downloading HateDay dataset...")
    get_hateday()

    typer.echo("All datasets downloaded.")


@app.command()
def simulation(tune_hparams: bool = False):
    """Measure, on real datasets, how much manipulation is needed to pass an audit.

    For every (dataset, sensitive attribute) pair, splits the data into a
    training set and a candidate set, trains a gradient boosting classifier, and
    samples an audit set from the candidates. Then, for a range of target
    demographic parity gaps, counts the number of prediction flips a dishonest
    platform needs to reach that target, both when it manipulates the whole
    candidate set (R²esPIR, where the manipulation must absorb the
    concentration term gamma) and when it only manipulates the audit set
    (vanilla black-box audit).

    Writes the per-target records to `generated/dpg_manipulation.csv`. The
    per-dataset quantities that `manipulation-simulation-table` needs are
    produced by `datasets-infos`.

    With `--tune-hparams`, the classifier hyperparameters are selected by a
    10-iteration randomized search with 5-fold CV instead of using the defaults.
    """

    datasets = get_credit_card_clients() | get_compas() | get_hateday()  # noqa: F821
    records: list = []

    for i, (name, (X, A, y)) in enumerate(datasets.items()):
        print(name)

        dataset_records = run_respir_simulation(
            X,
            A,
            y,
            delta=DELTA,
            candidates_size=CANDIDATES_SIZE,
            audit_candidates_ratio=AUDIT_CANDIDATES_RATIO,
            seed=42 + i,
            tune_hparams=tune_hparams,
        )

        records.append(dataset_records.with_columns(dataset=pl.lit(name)))

    records: pl.DataFrame = pl.concat(records)
    records.write_csv("generated/dpg_manipulation.csv")


@app.command()
def datasets_infos(tune_hparams: bool = False):
    """Measure the audit quantities of every (dataset, sensitive attribute) pair.

    For each pair, splits the data into a training set and a candidate set,
    trains a gradient boosting classifier and samples an audit set from the
    candidates, exactly as `simulation` does. Then reports the sizes of those
    sets, the size of their minority group, the demographic parity gap of the
    model on the candidate set (d_C) and on the audit set (d_S), and its
    accuracy.

    Writes `generated/datasets_infos.csv`, the input of
    `manipulation-simulation-table`.

    With `--tune-hparams`, the classifier hyperparameters are selected by a
    10-iteration randomized search with 5-fold CV instead of using the defaults.
    """

    datasets = get_credit_card_clients() | get_compas() | get_hateday()  # noqa: F821
    infos: list = []

    for i, (name, (X, A, y)) in enumerate(datasets.items()):
        print(name)

        info, _ = dataset_info(
            X,
            A,
            y,
            delta=DELTA,
            candidates_size=CANDIDATES_SIZE,
            audit_candidates_ratio=AUDIT_CANDIDATES_RATIO,
            seed=42 + i,
            tune_hparams=tune_hparams,
        )
        info["dataset"] = name

        infos.append(info)

    pl.from_records(infos).write_csv("generated/datasets_infos.csv")


# name -> (n_min, N_min), for a fixed n=400, N=2_000.
BOUNDS_SCENARIOS = {
    "balanced": (200, 1_000),
    "unbalanced": (40, 200),
}

# name -> (N, n, N_min, n_min)
APPENDIX_SCENARIOS = {
    "main": (20_000, 400, 10_000, 200),
    "appendixA": (50_000, 1_000, 25_000, 500),
    "appendixB": (10_000, 200, 4_000, 80),
}


@app.command()
def plot_bounds(scenario: Literal["balanced", "unbalanced", "all"] = "all"):
    """Plot the theoretical manipulation bounds for a vanilla black-box audit and for R²esPIR.

    For a grid of excess demographic parity gaps to hide, draws (i) the lower
    bound on the number of prediction flips m the platform must perform
    (Prop. 3.1 for the vanilla audit, Thm. 4.1 for R²esPIR) and (ii) the
    resulting detection probability of Eq. (3) for several numbers k of
    verification queries. The R²esPIR bound is larger because the manipulation
    must cover the whole candidate set and absorb the concentration term
    gamma = sqrt(2 ln(4 / delta) / n_min).

    The audit sizes are fixed (n = 400, N = 2000) and the scenario only selects
    the group imbalance (n_min, N_min): `balanced` for evenly split groups,
    `unbalanced` for a small minority group, `all` for both. Writes
    `generated/fig_manipulation_costs_<scenario>.pdf` and
    `generated/fig_detection_probability_<scenario>.pdf`.
    """

    if scenario == "all":
        names = list(BOUNDS_SCENARIOS)
    elif scenario in BOUNDS_SCENARIOS:
        names = [scenario]
    else:
        raise ValueError(f"Unknown {scenario = }")

    for name in names:
        n_min, N_min = BOUNDS_SCENARIOS[name]

        manipulation_bounds(
            delta=0.05,
            n=400,
            n_min=n_min,
            N=2_000,
            N_min=N_min,
            ks=[1, 3, 5],
            q=0.8,
            outdir=Path("generated/"),
            suffix=f"_{name}",
        )


@app.command()
def plot_appendix_scenarios():
    """Plot the same bounds as `plot-bounds`, for the audit sizes of the appendix.

    Re-draws the manipulation cost and detection probability figures for each
    (N, n, N_min, n_min) scenario of the appendix, which vary the candidate set
    and audit set sizes rather than the group imbalance. Every scenario is
    titled and exported as PNG to `generated/fig_*_<scenario>.png`.

    Because those scenarios span several orders of magnitude of m, the axis
    limits and the log scale tuned for the main figures are dropped here, and
    the detection probability is plotted for a perfect verification rate
    (q = 1) over a wider range of gaps to hide.
    """

    for name, (N, n, N_min, n_min) in APPENDIX_SCENARIOS.items():
        manipulation_bounds(
            delta=0.05,
            n=n,
            n_min=n_min,
            N=N,
            N_min=N_min,
            ks=[1, 2, 3],
            q=1.0,
            delta_max=0.08,
            num=161,
            outdir=Path("generated/"),
            suffix=f"_{name}",
            ext="png",
            title=name,
            # the appendix scenarios span several orders of magnitude of m, so
            # the axis bounds tuned for the main figures do not apply.
            costs_ylim=None,
            costs_yscale="linear",
            detection_ylim=None,
        )


@app.command()
def manipulation_simulation_table(plot: bool = False):
    """Turn the simulation results into the manipulation cost/detection table of the paper.

    Reads the per-dataset quantities produced by `datasets-infos` in
    `generated/datasets_infos.csv` and, for each dataset, applies the
    theoretical bounds to its own audit sizes (n, N, n_min, N_min). The gap to
    hide is set to half of the model's measured demographic parity gap on the
    candidate set, and the bounds are capped by that original bias. For several
    numbers k of verification queries, reports the number of flips and the
    detection probability under both the vanilla and R²esPIR audits.

    Writes `generated/summary_results.csv`, with the probabilities preformatted
    for a LaTeX table. With `--plot`, also saves the per-dataset manipulation
    cost and detection figures to `generated/`.
    """

    table_dp_to_hide(
        csv_path=Path("generated/datasets_infos.csv"),
        outdir=Path("generated/"),
        plot=plot,
    )


@app.command()
def plot_detection(
    dataset: list[str] = ["CCD_marriage", "COMPAS_race"],
    k: list[int] = [5, 10, 20, 50],
):
    """Plot the detection probability as a function of the number of canaries (Fig. 2).

    Reads the per-dataset quantities produced by `datasets-infos` in
    `generated/datasets_infos.csv` and, for each selected (dataset, sensitive
    attribute) pair, computes the number of flips its platform needs to hide
    half of its demographic parity gap, under a vanilla black-box audit
    (Prop. 3.1) and under R²esPIR (Thm. 4.1). Eq. (3) then turns those flip
    counts into a detection probability for every number k of canaries owned by
    the auditor.

    Writes `generated/fig_detection_vs_canaries.pdf`. The pairs are those of
    `generated/datasets_infos.csv`; the figure of the paper shows
    `CCD_marriage` and `COMPAS_race`.
    """

    plot_detection_vs_canaries(
        csv_path=Path("generated/datasets_infos.csv"),
        outdir=Path("generated/"),
        datasets=dataset,
        ks=k,
    )


if __name__ == "__main__":
    app()
