from __future__ import annotations

import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
PLOTTER = ROOT / "output" / "plot_ae_figures.py"


def required_result(pattern: str) -> Path:
    matches = sorted((ROOT / "results").glob(pattern))
    assert matches, f"missing result fixture matching {pattern}"
    return matches[0]


V2_CSV = required_result("*_v2/*_v2_results.csv")
FOOTPRINT_CSV = required_result("*_12_rerun/*_12_plot_points.csv")

EXPECTED_FIGURES = {
    "fig_ae_mapping_performance",
    "fig_ae_mapping_footprint",
    "fig_ae_mapping_group_size",
    "fig_ae_sf_mapping_pressure",
    "fig_ae_fs_capacity_window",
    "fig_ae_fs_sharing_scope",
}


def test_ae_plotter_lists_six_figures() -> None:
    result = subprocess.run(
        [sys.executable, str(PLOTTER), "--list"],
        cwd=ROOT,
        text=True,
        capture_output=True,
    )

    assert result.returncode == 0, result.stderr
    assert set(result.stdout.split()) == EXPECTED_FIGURES


def test_ae_plotter_generates_jpg_and_helvetica_eps(tmp_path: Path) -> None:
    result = subprocess.run(
        [
            sys.executable,
            str(PLOTTER),
            "--csv",
            str(V2_CSV),
            "--footprint-csv",
            str(FOOTPRINT_CSV),
            "--out-dir",
            str(tmp_path),
            "--formats",
            "jpg",
            "eps",
        ],
        cwd=ROOT,
        text=True,
        capture_output=True,
    )

    assert result.returncode == 0, result.stderr
    for figure in EXPECTED_FIGURES:
        jpg = tmp_path / f"{figure}.jpg"
        eps = tmp_path / f"{figure}.eps"
        assert jpg.exists(), f"missing {jpg}"
        assert eps.exists(), f"missing {eps}"
        eps_text = eps.read_text(encoding="latin-1")
        assert "/Helvetica" in eps_text
        assert "/Arial" not in eps_text
        assert "/FontType 42" not in eps_text


def test_ae_plotter_default_paths_fall_back_to_existing_legacy_csvs(tmp_path: Path) -> None:
    result = subprocess.run(
        [
            sys.executable,
            str(PLOTTER),
            "--out-dir",
            str(tmp_path),
            "--formats",
            "eps",
        ],
        cwd=ROOT,
        text=True,
        capture_output=True,
    )

    assert result.returncode == 0, result.stderr
    assert {path.stem for path in tmp_path.glob("*.eps")} == EXPECTED_FIGURES
