import numpy as np
import polars as pl

from respir.data import dataset_info, demographic_parity_gap


def manipulate_dpg(y_pred: np.ndarray, A: np.ndarray, target: int = 0.01, seed=42):
    dp_gap = demographic_parity_gap(y_pred, A)
    base_rate = 1 / (A == 0).sum(), 1 / (A == 1).sum()
    rng = np.random.default_rng(seed)

    if abs(dp_gap) < abs(target):
        return y_pred

    # Compute the impact of flipping 0s to 1s (a.k.a. positive/negative
    # discrimination) and the associated sensitivity. In each case, both the
    # actions would manipulate the dpg towards the desired target dpg, but
    # `optimal` is the action that has the highest sensitivity.
    if dp_gap < 0:
        flip_candidates = {
            "0->1": ((A == 1) & (y_pred == 0), base_rate[1]),
            "1->0": ((A == 0) & (y_pred == 1), base_rate[0]),
        }
        optimal = "0->1" if base_rate[1] > base_rate[0] else "1->0"

    elif dp_gap >= 0:
        flip_candidates = {
            "0->1": ((A == 0) & (y_pred == 0), -base_rate[0]),
            "1->0": ((A == 1) & (y_pred == 1), -base_rate[1]),
        }
        optimal = "0->1" if base_rate[0] > base_rate[1] else "1->0"

    # Now that we know the impact of the different label flips, we compute how
    # many flips we need to perform. There are two steps: if positive
    # discrimination is not enough, we go to negative discrimination (and
    # vice-versa).
    #
    # Step 1: make as many manipulations as possible with the most sensitive
    # manipulation
    n_sufficient = int(np.ceil((1 / flip_candidates[optimal][1]) * (target - dp_gap)))
    n_manipulations = min(n_sufficient, np.sum(flip_candidates[optimal][0]))

    if n_sufficient <= 0:
        return y_pred

    flip_idx = rng.choice(
        np.arange(len(y_pred))[flip_candidates[optimal][0]],
        size=min(n_sufficient, np.sum(flip_candidates[optimal][0])),
        replace=False,
    )
    y_pred[flip_idx] = 1 - y_pred[flip_idx]
    temp_dpg = demographic_parity_gap(y_pred, A)
    tmp_flip_idx = flip_idx

    # Step 2: If this was not enough, use the other manipulations
    if n_manipulations < n_sufficient:
        optimal = "0->1" if optimal == "1->0" else "1->0"
        n_sufficient = int(
            np.ceil((1 / flip_candidates[optimal][1]) * (abs(temp_dpg) - abs(target)))
        )
        n_manipulations = min(n_sufficient, np.sum(flip_candidates[optimal][0]))

        flip_idx = rng.choice(
            np.arange(len(y_pred))[flip_candidates[optimal][0]],
            size=min(n_sufficient, np.sum(flip_candidates[optimal][0])),
            replace=False,
        )
        y_pred[flip_idx] = 1 - y_pred[flip_idx]
        assert (flip_idx & tmp_flip_idx).sum() == 0

    return y_pred


def run_respir_simulation(
    X: pl.DataFrame,
    A: pl.Series,
    y: pl.Series,
    delta: float = 0.05,
    candidates_size: float = 0.3,
    audit_candidates_ratio: float = 0.2,
    seed=42,
    tune_hparams: bool = False,
):
    """Count the prediction flips needed to hide a range of demographic parity gaps.

    Trains a model and samples an audit set with `dataset_info`, then, for every
    target gap, manipulates its predictions both on the whole candidate set
    (R²esPIR, where the manipulation must absorb the concentration term gamma)
    and on the audit set only (vanilla black-box audit).
    """

    info, setup = dataset_info(
        X,
        A,
        y,
        delta=delta,
        candidates_size=candidates_size,
        audit_candidates_ratio=audit_candidates_ratio,
        seed=seed,
        tune_hparams=tune_hparams,
    )
    y_pred, A_test, audit_set = setup.y_pred, setup.A_test, setup.audit_set
    target_correction = setup.target_correction
    dpg_candidate_set = info["d_C"]

    records = []

    for target in np.logspace(-4, -0.5, num=30):
        # Compute the corrected target and get the signs right
        corrected_target = np.sign(dpg_candidate_set) * max(
            target - target_correction, 0
        )
        target = np.sign(dpg_candidate_set) * abs(target)

        # Manipulating on the candidates set
        y_manip = manipulate_dpg(y_pred.copy(), A_test, target=corrected_target)
        n_manip = np.sum(y_pred != y_manip)
        dpg_manip = demographic_parity_gap(y_manip, A_test)
        dpg_manip_audit = demographic_parity_gap(y_manip[audit_set], A_test[audit_set])

        # Manipulating directly on the audit set
        y_manip = manipulate_dpg(
            y_pred[audit_set].copy(), A_test[audit_set], target=target
        )
        n_manip_vanila = np.sum(y_pred[audit_set] != y_manip)
        dpg_manip_audit_vanilla = demographic_parity_gap(y_manip, A_test[audit_set])

        records.append(
            {
                "target_dpg": target,
                "target_correction": target_correction,
                "corrected_target_dpg": corrected_target,
                "n_manipulations": n_manip,
                "n_manipulations_vanilla": n_manip_vanila,
                "manipulated_dpg_candidate_set": dpg_manip,
                "manipulated_dpg_audit_set": dpg_manip_audit,
                "dpg_manip_audit_vanilla": dpg_manip_audit_vanilla,
                "delta": delta,
                "seed": seed,
                "n_candidates": info["n_candidates"],
                "audit_budget": info["audit_budget"],
            }
        )

    return pl.from_records(records).with_columns(
        accuracy=pl.lit(info["accuracy_C"]),
        balanced_accuracy=info["balanced_accuracy_C"],
        dpg_candidate_set=info["d_C"],
        dpg_audit_set=info["d_S"],
    )
