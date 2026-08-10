from dataclasses import dataclass
from functools import partial
from pathlib import Path
from time import perf_counter
from typing import Any

import numpy as np
import polars as pl
import torch
from huggingface_hub import hf_hub_download
from scipy import stats
from sentence_transformers import SentenceTransformer
from sklearn.linear_model import LogisticRegression
from sklearn.metrics import accuracy_score, balanced_accuracy_score
from sklearn.model_selection import RandomizedSearchCV, train_test_split
from sklearn.pipeline import make_pipeline
from sklearn.preprocessing import StandardScaler
from skrub import tabular_pipeline
from ucimlrepo import fetch_ucirepo


################################################################################
# CCD                                                                          #
################################################################################
def get_credit_card_clients():
    # Default of Credit Card Clients
    # https://archive.ics.uci.edu/dataset/350/default+of+credit+card+clients
    if Path("data/credit_card_default.parquet").exists():
        data = pl.read_parquet("data/credit_card_default.parquet")
    else:
        default_of_credit_card_clients = fetch_ucirepo(id=350)
        data = pl.from_dataframe(
            default_of_credit_card_clients.data.features
        ).with_columns(
            target=pl.from_dataframe(
                default_of_credit_card_clients.data.targets
            ).to_series()
        )
        data.write_parquet("data/credit_card_default.parquet")

    y = (
        data.select("target")
        .to_series()
        .cast(pl.Int64)  # result: 0 => no_default next month, 1 => defaults next month
    )
    datasets = {
        "CCD_gender": (
            data.drop("target", "X2"),
            # 0 => male, 1 => female
            (data.select("X2") == 2).to_series().cast(pl.Int64),
            y,
        ),
        "CCD_education": (
            data.drop("target", "X3"),
            # 0 => graduate or bachelor degree, 1 => highschool or no degree
            (data.select("X3") <= 2).to_series().cast(pl.Int64),
            y,
        ),
        "CCD_marriage": (
            data.drop("target", "X4"),
            # 1 => single or other, 0 => married
            (data.select("X4") != 1).to_series().cast(pl.Int64),
            y,
        ),
        "CCD_age": (
            data.drop("target", "X5"),
            # 1 => more than 30 y.o., 0 => less than 30 y.o.
            (data.select("X5") > 30).to_series().cast(pl.Int64),
            y,
        ),
    }

    return datasets


################################################################################
# COMPAS                                                                       #
################################################################################
def get_compas():
    if Path("data/compas.csv").exists():
        data = pl.read_csv("data/compas.csv")
    else:
        data = pl.read_csv(
            "https://raw.githubusercontent.com/mlr-org/mlr3fairness/main/data-raw/compas-scores-two-years.csv"
        )
        data.write_csv("data/compas.csv")

    features = [
        "decile_score",
        "days_b_screening_arrest",
        "priors_count",
        "sex",
        "score_text",
        "age",
        "race",
        "c_charge_degree",
    ]
    datasets = {
        "COMPAS_gender": (
            data.select(features).drop("sex"),
            (data.select("sex") != "Male").to_series().cast(pl.Int64),
            data.select("two_year_recid").to_series().cast(pl.Int64),
        ),
        "COMPAS_race": (
            data.select(features).drop("race"),
            (data.select("race") == "Caucasian").to_series().cast(pl.Int64),
            data.select("two_year_recid").to_series().cast(pl.Int64),
        ),
    }

    return datasets


################################################################################
# HATEDAY                                                                      #
################################################################################
ST_MODEL = "sentence-transformers/distiluse-base-multilingual-cased-v2"
HATEDAY_REPO = "manueltonneau/hateday"
HATEDAY_FILE = "hateday_v2_hf_final.parquet"


def get_hateday():
    if not Path(f"data/{HATEDAY_FILE}").exists():
        # HateDay is a gated dataset: accept its conditions on
        # https://huggingface.co/datasets/manueltonneau/hateday and log in with
        # `uv run hf auth login` (or export HF_TOKEN) before running this.
        path = hf_hub_download(
            HATEDAY_REPO, HATEDAY_FILE, repo_type="dataset", local_dir="data/"
        )
        assert Path(path) == Path(f"data/{HATEDAY_FILE}")

    if not Path("generated/hateday_with_embeddings.parquet").exists():
        encode_data()

    data = pl.read_parquet("generated/hateday_with_embeddings.parquet").with_columns(
        pl.col("embedding")
        .arr.to_struct(fields=list(map(str, range(512))))
        .name.prefix_fields("embedding_")
        .struct.unnest(),
    )

    X = data.select(pl.selectors.starts_with("embedding_"))
    y = data.select(pl.col("class_clean") != 0).to_series()
    A = data.select(pl.col("lang_country_hateday") != "US").to_series()

    return {"hateday_distiluse-base_lang": (X, A, y)}


def encode_all(text: pl.Series, model: SentenceTransformer):
    return pl.Series(model.encode(text.to_list(), batch_size=128))


def encode_data():
    device = "cuda" if torch.cuda.is_available() else "cpu"
    print(f"Encoding the HateDay texts with {ST_MODEL} on {device}")
    model = SentenceTransformer(ST_MODEL, device=device)

    data = pl.read_parquet(f"data/{HATEDAY_FILE}")

    # Add embeddings
    start = perf_counter()
    encoded = data.with_columns(
        embedding=pl.col("text").map_batches(
            partial(encode_all, model=model), pl.Array(pl.Float32, 512)
        ),
        model=pl.lit(ST_MODEL),
    )
    end = perf_counter()
    print(encoded)
    print(f"Encoded {len(encoded)} items in {end - start} s")

    encoded.write_parquet("generated/hateday_with_embeddings.parquet")


################################################################################
# AUDIT SETUP                                                                  #
################################################################################
def demographic_parity_gap(y_pred: np.ndarray, A: np.ndarray):
    return np.mean(y_pred[A == 1]) - np.mean(y_pred[A == 0])


@dataclass
class AuditSetup:
    """Everything a simulation needs beyond the summary quantities of `dataset_info`.

    The indices in `train` and `test` (the candidate set) index the whole
    dataset, whereas `audit_set` indexes the candidate set, i.e. `X_test`,
    `y_test` and `A_test`.
    """

    model: Any
    train: np.ndarray
    test: np.ndarray
    audit_set: np.ndarray
    X_test: pl.DataFrame
    y_test: np.ndarray
    A_test: np.ndarray
    y_pred: np.ndarray
    # Corrective term the platform has to use to pass with proba 1 - delta when
    # doing the respir audit.
    target_correction: float


def dataset_info(
    X: pl.DataFrame,
    A: pl.Series,
    y: pl.Series,
    delta: float = 0.05,
    candidates_size: float = 0.3,
    audit_candidates_ratio: float = 0.2,
    seed=42,
    tune_hparams: bool = False,
) -> tuple[dict, AuditSetup]:
    """Describe the audit of one (dataset, sensitive attribute) pair.

    Splits the data into a training set and a candidate set, trains a gradient
    boosting classifier on the former and samples an audit set from the latter.
    Returns the per-dataset quantities the theoretical bounds need (n, N, n_min,
    N_min, d_C, d_S, accuracy, ...) together with the split, the fitted model
    and its predictions, so that `run_respir_simulation` can manipulate those
    predictions without redoing the training.

    With `tune_hparams`, the classifier hyperparameters are selected by a
    10-iteration randomized search with 5-fold CV instead of using the defaults.
    """

    # Split
    train, test = train_test_split(
        np.arange(len(X)),
        test_size=candidates_size,
        random_state=seed,
        stratify=A,
    )
    n_candidates = len(test)
    audit_budget = int(audit_candidates_ratio * n_candidates)

    ################################################################################
    # MODEL TRAINING                                                               #
    ################################################################################
    model = tabular_pipeline("classifier")
    params = {
        "histgradientboostingclassifier__learning_rate": stats.loguniform(
            0.01, 1
        ),  # default 0.1
        "histgradientboostingclassifier__max_leaf_nodes": stats.randint(
            20, 50
        ),  # default 31
        "histgradientboostingclassifier__max_iter": stats.randint(
            10, 1_000
        ),  # default 100
    }
    if tune_hparams:
        search = RandomizedSearchCV(
            model, param_distributions=params, n_iter=10, cv=5, refit=True
        ).fit(X[train], y[train])
        model = search.best_estimator_

    else:
        model = model.fit(X[train], y[train])

    ################################################################################
    # AUDIT                                                                        #
    ################################################################################
    y_test = y[test].to_numpy()
    A_test = A[test].to_numpy()
    X_test = X[test]

    _, audit_set = train_test_split(
        np.arange(len(X_test)),
        test_size=audit_budget,
        random_state=seed * 5,
        stratify=A_test,
    )

    base_rates_audit_set = (
        1 / (A_test[audit_set] == 0).sum(),
        1 / (A_test[audit_set] == 1).sum(),
    )
    y_pred = model.predict(X_test)

    # Corrective term the platform has to use to pass with proba 1 - delta when
    # doing the respir audit.
    target_correction = np.sqrt(2 * max(base_rates_audit_set) * np.log(4 / delta))

    info = {
        "audit_budget": audit_budget,
        "n_candidates": n_candidates,
        "n_train": len(train),
        "dataset_size": len(X),
        "n_min": min((A_test[audit_set] == 0).sum(), (A_test[audit_set] == 1).sum()),
        "N_min": min((A_test == 0).sum(), (A_test == 1).sum()),
        "d_C": demographic_parity_gap(y_pred, A_test),
        "d_S": demographic_parity_gap(y_pred[audit_set], A_test[audit_set]),
        "accuracy_C": accuracy_score(y_test, y_pred),
        "balanced_accuracy_C": balanced_accuracy_score(y_test, y_pred),
        f"ResPIR_advantage@{delta}": target_correction,
        "base_rate_C": np.mean(A_test == 1),
    }
    setup = AuditSetup(
        model=model,
        train=train,
        test=test,
        audit_set=audit_set,
        X_test=X_test,
        y_test=y_test,
        A_test=A_test,
        y_pred=y_pred,
        target_correction=target_correction,
    )

    return info, setup


def make_split_idx(
    X, y, A, proportions: tuple[float, float] = (0.15, 0.15), seed: int = 42
) -> tuple[pl.DataFrame, pl.DataFrame, pl.DataFrame]:
    train, test = train_test_split(
        np.arange(len(X)),
        test_size=int(proportions[1] * len(X)),
        random_state=seed,
        stratify=A,
    )
    train, val = train_test_split(
        train,
        test_size=int(proportions[0] * len(X)),
        random_state=seed,
        stratify=A[train],
    )

    return train, val, test


def make_data() -> dict[str, tuple[pl.DataFrame, pl.Series, pl.Series]]:
    # Read the data
    data = pl.read_parquet("generated/hateday_with_embeddings.parquet").with_columns(
        pl.col("embedding")
        .arr.to_struct(fields=list(map(str, range(512))))
        .name.prefix_fields("embedding_")
        .struct.unnest(),
    )

    X = data.select(pl.selectors.starts_with("embedding_"))
    y = data.select(pl.col("class_clean") != 0).to_series()
    A = data.select(pl.col("lang_country_hateday")).to_series().rank("dense")

    train, val, test = make_split_idx(X, y, A)

    return {
        "train_idx": train,
        "val_idx": val,
        "test_idx": test,
        "train": (X[train], y[train], A[train]),
        "val": (X[val], y[val], A[val]),
        "test": (X[test], y[test], A[test]),
    }


def explore_data():
    data = pl.read_parquet("generated/hateday_with_embeddings.parquet")
    # skrub.TableReport(data).open()

    # Evaluate the twitter moderation algorithm
    print("Performance of Twitter moderation per country")
    print(
        data.with_columns(y_true=pl.col("class_clean") == 2)
        .group_by("lang_country_hateday")
        .agg(
            accuracy=(pl.col("twitter_hate") == pl.col("y_true")).mean(),
            tpr=(pl.col("twitter_hate") == pl.col("y_true"))
            .filter(y_true=pl.lit(True))
            .mean(),
            tnr=(pl.col("twitter_hate") == pl.col("y_true"))
            .filter(y_true=pl.lit(False))
            .mean(),
        )
        .with_columns(balanced_accuracy=(pl.col("tpr") + pl.col("tnr")) / 2)
        .sort("lang_country_hateday")
    )

    print("True positive rate of twitter moderation per hate target")
    print(
        data.with_columns(y_true=pl.col("class_clean") == 2)
        .filter(class_clean=2)
        .group_by("target_category")
        .agg(
            total=pl.len(),
            tpr=(pl.col("twitter_hate") == pl.col("y_true"))
            .filter(y_true=pl.lit(True))
            .mean(),
        )
        .sort("total", descending=True)
    )

    # Evaluate a simple logistic regression sklearn model
    splits = make_data()
    X_train, y_train, A_train = splits["train"]
    model = make_pipeline(StandardScaler(), LogisticRegression())
    model.fit(X_train, y_train)

    preds: pl.DataFrame = data[splits["test_idx"]]
    preds = preds.with_columns(
        y_pred=pl.col("embedding").map_batches(
            lambda embeddings: model.predict(embeddings.to_numpy())
        ),
        # y_pred=pl.lit(1),
    )

    print("Performance of a LogisiticRegression model per contry")
    print(
        preds.with_columns(y_true=pl.col("class_clean") == 2)
        .group_by("lang_country_hateday")
        .agg(
            accuracy=(pl.col("y_pred") == pl.col("y_true")).mean(),
            tpr=(pl.col("y_pred") == pl.col("y_true"))
            .filter(y_true=pl.lit(True))
            .mean(),
            tnr=(pl.col("y_pred") == pl.col("y_true"))
            .filter(y_true=pl.lit(False))
            .mean(),
        )
        .with_columns(balanced_accuracy=(pl.col("tpr") + pl.col("tnr")) / 2)
        .sort("lang_country_hateday")
    )

    print("True positive rate of a LogisiticRegression model per hate target")
    print(
        preds.with_columns(y_true=pl.col("class_clean") == 2)
        .filter(class_clean=2)
        .group_by("target_category")
        .agg(
            total=pl.len(),
            tpr=(pl.col("y_pred") == pl.col("y_true"))
            .filter(y_true=pl.lit(True))
            .mean(),
        )
        # .sort("total", descsending=True)
        .sort("tpr", descending=True)
    )
    preds.write_parquet("preds.parquet")
