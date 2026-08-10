"""
Plots for the paper:
1) lower bound on # output flips needed to pass (vanilla vs R²esPIR/RESPIR)
2) detection probability using Eq. (3): 1 - (1 - q m/denom)^k

Formulas match Ma_PARADe_FAccT_2025.pdf:
- Prop. 3.1: m_vanilla >= ceil((|d_true| - eps) * n_min)
- Thm. 4.1: m_respir  >= ceil((|d_C,true| - eps + gamma) * N_min)
  with gamma = sqrt( 2 ln(4/delta) / n_min )
- Eq. (3): P_detect = 1 - (1 - q m/denom)^k
"""

import re
from collections.abc import Sequence
from pathlib import Path

import matplotlib.pyplot as plt
import numpy as np
import pandas as pd
from matplotlib.lines import Line2D

suffix = "_nothing"  # for output filenames


def gamma(delta: float, n_min: int) -> float:
    return float(np.sqrt(2.0 * np.log(4.0 / delta) / n_min))


def m_vanilla(
    delta_excess: np.ndarray,
    n_min: int,
    original_bias: float = 1,
) -> np.ndarray:
    return np.ceil(np.maximum(np.minimum(delta_excess, original_bias), 0.0) * n_min)


def m_respir(
    delta_excess: np.ndarray,
    N_min: int,
    n_min: int,
    delta: float,
    original_bias: float = 1,
) -> np.ndarray:
    g = gamma(delta, n_min)
    return np.ceil(
        np.maximum(np.minimum(delta_excess + g, original_bias), 0.0) * N_min
    )  # delta_excess + gamma is capped into [0, original_bias] because it can't be negative or larger than original bias


def p_detect_eq3(m: np.ndarray, denom: float, k: int, q: float = 1.0) -> np.ndarray:
    frac = np.clip(q * (m / denom), 0.0, 1.0)
    return 1.0 - np.power(1.0 - frac, k)


def manipulation_bounds(
    eps: float = 0.0,
    delta: float = 0.05,
    n: int = 400,
    n_min: int = 200,
    N: int = 2000,
    N_min: int = 1000,
    q: float = 0.8,
    ks: list = None,
    delta_max: float = 0.10,
    num: int = 5000,
    outdir: str = ".",
    suffix: str = "_nothing",
    ext: str = "pdf",
    title: str | None = None,
    costs_ylim: tuple[float, float] | None = (0.7, 400),
    costs_yscale: str = "log",
    detection_ylim: tuple[float, float] | None = (-0.01, 0.5),
):
    if ks is None:
        ks = [1, 3, 5]

    outdir = Path(outdir)
    outdir.mkdir(parents=True, exist_ok=True)

    Delta = np.linspace(0, delta_max, num)  # Δ := |d| - eps

    mV = m_vanilla(Delta, n_min)
    mR = m_respir(Delta, N_min, n_min, delta)

    # --- Fig 1: flip complexity ---
    plt.figure()
    plt.plot(Delta, mV, label="Vanilla black-box audit (Prop. 3.1)")
    plt.plot(Delta, mR, label="R²esPIR audit (Thm. 4.1)")
    plt.xlabel(r"Excess demographic parity gap to hide")
    plt.ylabel(r"Lower bound on # flips $m$")
    if title:
        plt.title(
            f"Manipulation effort ({title}): {N=}, {n=}, {N_min=}, {n_min=}, {delta=}"
        )
    if costs_ylim:
        plt.ylim(list(costs_ylim))
    plt.legend(loc="lower right")
    plt.yscale(costs_yscale)
    plt.grid(True, alpha=0.3)
    plt.savefig(
        outdir / f"fig_manipulation_costs{suffix}.{ext}",
        dpi=220,
        bbox_inches="tight",
    )
    plt.close()

    # --- Fig 2: detection probability (Eq. 3) ---
    # NOTE: In the paper, Eq. (3) is derived for an audit set of size n (so denom=n).
    # In Section 4, the text discusses detection increasing with m/N; to match that discussion,
    # we plot RESPIR with denom=N (flips spread over the candidate set).
    from matplotlib.lines import Line2D

    plt.figure()
    colors = plt.rcParams["axes.prop_cycle"].by_key()["color"]

    # Plot lines (no labels here; we’ll build legend manually)
    vanilla_handles = []
    respir_handles = []
    vanilla_labels = []
    respir_labels = []

    for idx, k in enumerate(ks):
        c = colors[idx % len(colors)]

        pV = p_detect_eq3(mV, denom=n, k=k, q=q)
        pR = p_detect_eq3(mR, denom=N, k=k, q=q)

        plt.plot(Delta, pV, color=c, linestyle="-")
        plt.plot(Delta, pR, color=c, linestyle="--")

        # legend entries (one per line style)
        vanilla_handles.append(Line2D([0], [0], color=c, linestyle="-"))
        respir_handles.append(Line2D([0], [0], color=c, linestyle="--"))
        vanilla_labels.append(f"Vanilla, k={k}")
        respir_labels.append(f"R²esPIR, k={k}")

    # 2-column legend: first all Vanilla (left col), then all RESPIR (right col)
    handles = vanilla_handles + respir_handles
    labels = vanilla_labels + respir_labels
    plt.legend(
        handles,
        labels,
        ncol=2,
        fontsize=8,
        columnspacing=1.5,
        handlelength=2.5,
        loc="upper left",
    )
    plt.xlabel(r"Excess demographic parity gap to hide")
    plt.ylabel(r"Detection probability (Eq. 3)")
    if title:
        plt.title(f"Detection ({title}): {q=}, {delta=}")
    if detection_ylim:
        plt.ylim(list(detection_ylim))
    plt.grid(True, alpha=0.3)
    plt.savefig(
        outdir / f"fig_detection_probability{suffix}.{ext}",
        dpi=220,
        bbox_inches="tight",
    )
    plt.close()

    print("Wrote:")
    print(" -", outdir / f"fig_manipulation_costs{suffix}.{ext}")
    print(" -", outdir / f"fig_detection_probability{suffix}.{ext}")


def plot_dp_to_hide(
    n,
    n_min,
    N,
    N_min,
    delta_max,
    original_bias,
    suffix: str,
    q=0.8,
    ks=[1, 3, 5],
    num=5000,
    outdir: Path | None = None,
    delta=0.20,
    eps=0.0,
):

    Delta = np.linspace(0, delta_max, num)  # Δ := |d| - eps

    mV = m_vanilla(Delta, n_min, original_bias)
    mR = m_respir(Delta, N_min, n_min, delta, original_bias)

    # --- Fig 1: flip complexity ---
    if outdir:
        outdir.mkdir(parents=True, exist_ok=True)

        plt.figure()
        plt.plot(Delta, mV, label="Vanilla black-box audit (Prop. 3.1)")
        plt.plot(Delta, mR, label="R²esPIR audit (Thm. 4.1)")
        plt.xlabel(r"Excess demographic parity gap to hide")
        plt.ylabel(r"Lower bound on # flips $m$")
        # plt.title("Manipulation effort vs unfairness to hide")
        plt.legend(loc="lower right")
        # plt.yscale("log")
        plt.grid(True, alpha=0.3)
        plt.savefig(
            outdir / f"fig_manipulation_costs{suffix}.pdf", dpi=220, bbox_inches="tight"
        )
        plt.close()

    # --- Fig 2: detection probability (Eq. 3) ---
    # NOTE: In the paper, Eq. (3) is derived for an audit set of size n (so denom=n).
    # In Section 4, the text discusses detection increasing with m/N; to match that discussion,
    # we plot RESPIR with denom=N (flips spread over the candidate set).
    if outdir:
        plt.figure()
        colors = plt.rcParams["axes.prop_cycle"].by_key()["color"]

        # Plot lines (no labels here; we’ll build legend manually)
        vanilla_handles = []
        respir_handles = []
        vanilla_labels = []
        respir_labels = []

        for idx, k in enumerate(ks):
            c = colors[idx % len(colors)]

            pV = p_detect_eq3(mV, denom=n, k=k, q=q)
            pR = p_detect_eq3(mR, denom=N, k=k, q=q)

            plt.plot(Delta, pV, color=c, linestyle="-")
            plt.plot(Delta, pR, color=c, linestyle="--")

            # legend entries (one per line style)
            vanilla_handles.append(Line2D([0], [0], color=c, linestyle="-"))
            respir_handles.append(Line2D([0], [0], color=c, linestyle="--"))
            vanilla_labels.append(f"Vanilla, k={k}")
            respir_labels.append(f"R²esPIR, k={k}")

        # 2-column legend: first all Vanilla (left col), then all RESPIR (right col)
        handles = vanilla_handles + respir_handles
        labels = vanilla_labels + respir_labels
        plt.legend(
            handles,
            labels,
            ncol=2,
            fontsize=8,
            columnspacing=1.5,
            handlelength=2.5,
            loc="upper left",
        )
        plt.xlabel(r"Excess demographic parity gap to hide")
        plt.ylabel(r"Detection probability (Eq. 3)")
        plt.grid(True, alpha=0.3)
        plt.savefig(
            outdir / f"fig_detection_probability{suffix}.pdf",
            dpi=220,
            bbox_inches="tight",
        )
        plt.close()

    g = gamma(delta, n_min)
    print("gamma:", g)

    if delta_max + g < original_bias:
        computed_confidence = delta
        print("Using input delta as computed confidence:", computed_confidence)
    else:  # delta_max + g >= original_bias
        max_width_possible = original_bias - delta_max
        print("max_width_possible =", max_width_possible)
        computed_confidence = 4.0 * np.exp(-((max_width_possible) ** 2) * n_min / 2.0)
        print("Adjusted computed confidence = ", computed_confidence)
        # just verify
        g = gamma(computed_confidence, n_min)
        print("Adjusted computed confidence to:", computed_confidence)

    local_results = []
    for k in [5, 10, 20, 50]:
        pV = p_detect_eq3(mV, denom=n, k=k, q=q)
        pR = p_detect_eq3(mR, denom=N, k=k, q=q)

        print(
            "[respir] #modifs:",
            int(mR[-1]),
            "detection probability:",
            pR[-1],
            "computed_confidence",
            computed_confidence,
        )  # print number of modifications for max delta
        print(
            "[vanilla] #modifs:", int(mV[-1]), "detection probability:", pV[-1]
        )  # print number of modifications for max delta
        local_results.append(
            [
                suffix,
                int(mR[-1]),
                int(mV[-1]),
                k,
                # escaped for direct paste into a LaTeX table
                rf"{100 * float(pR[-1]):.2f}\%",
                rf"{100 * float(pV[-1]):.2f}\%",
            ]
        )
    return local_results  # , "%.1f" % (100*float((pR[-1]-pV[-1])/max(pV[-1], 1e-10)))]


def plot_detection_vs_canaries(
    csv_path: Path,
    outdir: Path,
    datasets: Sequence[str],
    ks: Sequence[int] = (5, 10, 20, 50),
    q: float = 0.8,
    delta: float = 0.20,
    suffix: str = "",
    ext: str = "pdf",
):
    """Plot the detection probability as a function of the number of canaries k.

    One line style per (dataset, sensitive attribute) pair and one color per
    audit framework. Each pair contributes the number of flips its platform
    needs to hide half of its demographic parity gap, which Eq. (3) turns into
    a detection probability for every k.

    Expected columns in `csv_path` (as written by `datasets-infos`):
      - audit_budget   -> n
      - n_candidates   -> N
      - n_min          -> n_min
      - N_min          -> N_min
      - d_C            -> demographic parity gap on the candidate set
      - dataset        -> the pair to select on
    """

    df = pd.read_csv(csv_path).set_index("dataset")

    missing = [name for name in datasets if name not in df.index]
    if missing:
        raise ValueError(f"Unknown datasets: {missing}. Found: {list(df.index)}")

    outdir = Path(outdir)
    outdir.mkdir(parents=True, exist_ok=True)

    ks = np.asarray(ks)
    colors = plt.rcParams["axes.prop_cycle"].by_key()["color"]
    respir_color, vanilla_color = colors[0], colors[1]
    # one (line style, marker) per dataset, so that color is left to encode the
    # audit framework.
    styles = [("-", "o"), ("--", "s"), (":", "^"), ("-.", "D")]

    plt.figure(figsize=(5, 3.2))

    dataset_handles = []
    for name, (linestyle, marker) in zip(datasets, styles):
        row = df.loc[name]

        n, N = int(row["audit_budget"]), int(row["n_candidates"])
        n_min, N_min = int(row["n_min"]), int(row["N_min"])

        # The platform sets its tolerance to eps = |d_C| / 2, so the excess gap
        # to hide is |d_C| / 2 as well. Both bounds are capped by |d_C|, since
        # flipping the whole gap away is always enough.
        original_bias = abs(float(row["d_C"]))
        to_hide = np.asarray(0.5 * original_bias)
        m_v = m_vanilla(to_hide, n_min, original_bias)
        m_r = m_respir(to_hide, N_min, n_min, delta, original_bias)

        # Eq. (3) is derived for an audit set of size n, so the vanilla flips
        # are diluted in n. R²esPIR forces them onto the whole candidate set,
        # hence the denominator N.
        p_v = p_detect_eq3(m_v, denom=n, k=ks, q=q)
        p_r = p_detect_eq3(m_r, denom=N, k=ks, q=q)

        for probabilities, color in ((p_r, respir_color), (p_v, vanilla_color)):
            plt.plot(
                ks,
                100 * probabilities,
                color=color,
                linestyle=linestyle,
                marker=marker,
                markerfacecolor="none",
            )

        dataset_handles.append(
            Line2D(
                [0],
                [0],
                color="black",
                linestyle=linestyle,
                marker=marker,
                markerfacecolor="none",
                label=name.replace("_", " "),
            )
        )

    method_handles = [
        Line2D([0], [0], color=respir_color, label="R²esPIR"),
        Line2D([0], [0], color=vanilla_color, label="Vanilla"),
    ]

    plt.xticks(ks, [str(k) for k in ks])
    plt.xlabel(r"Number of canaries ($k$)")
    plt.ylabel("Detection probability (%)")
    plt.legend(
        handles=dataset_handles + method_handles,
        ncol=2,
        frameon=False,
        loc="lower left",
        bbox_to_anchor=(0.0, 1.01, 1.0, 0.2),
        mode="expand",
    )
    plt.savefig(
        outdir / f"fig_detection_vs_canaries{suffix}.{ext}",
        dpi=220,
        bbox_inches="tight",
    )
    plt.close()

    print("Wrote:", outdir / f"fig_detection_vs_canaries{suffix}.{ext}")


def slugify(s: str) -> str:
    """Safe suffix for filenames."""
    s = str(s).strip()
    s = s.replace(" ", "_")
    s = re.sub(r"[^A-Za-z0-9_\-\.]+", "", s)
    return s


def table_dp_to_hide(csv_path: Path, outdir: Path, plot: bool = False):
    """
    Batch-generate plots by iterating over datasets_infos.csv and calling
    plot_manipulation_and_detection for each dataset.

    Expected CSV columns:
      - audit_budget   -> n
      - n_candidates   -> N
      - n_min          -> n_min
      - N_min          -> N_min
      - dataset        -> used for suffix
    """
    df = pd.read_csv(csv_path)

    required = [
        "audit_budget",
        "n_candidates",
        "n_min",
        "N_min",
        "dataset",
        "d_C",
        "d_S",
        "dataset",
    ]
    missing = [c for c in required if c not in df.columns]
    if missing:
        raise ValueError(
            f"Missing columns in CSV: {missing}. Found columns: {list(df.columns)}"
        )

    if df.empty:
        print("No rows to process.")
        return

    failures = 0
    results = []
    results.append(
        [
            "dataset",
            "respir_nb_flips",
            "vanilla_nb_flips",
            "k",
            "respir_detection_prob",
            "vanilla_detection_prob",
        ]
    )  # , "detection_prob_relative_increase"])

    for i, row in df.iterrows():
        dataset = row["dataset"]
        suffix = "_" + slugify(dataset)

        # Map CSV -> plotting script args
        n = int(row["audit_budget"])
        N = int(row["n_candidates"])
        n_min = int(row["n_min"])
        N_min = int(row["N_min"])
        delta_max_C = abs(float(row["d_C"]))
        delta_max_S = abs(float(row["d_S"]))

        original_bias = delta_max_C
        delta_max = 0.50 * original_bias  # use 1% of the original bias as x-axis limit
        suffix = str(row["dataset"])

        print("Dataset:", dataset, "unfairness to hide =", delta_max)

        results.extend(
            plot_dp_to_hide(
                n,
                n_min,
                N,
                N_min,
                delta_max,
                original_bias,
                suffix,
                outdir=outdir if plot else None,
            )
        )

    with open(outdir / "summary_results.csv", "w") as f:
        for r in results:
            f.write(",".join([str(x) for x in r]) + "\n")

    if failures:
        raise SystemExit(f"Done with {failures} failures.")
