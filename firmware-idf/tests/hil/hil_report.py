"""PDF report generator for HIL test results."""

from __future__ import annotations

import datetime
from dataclasses import dataclass, field
from pathlib import Path
from typing import Optional

from reportlab.lib import colors
from reportlab.lib.pagesizes import A4
from reportlab.lib.units import mm
from reportlab.platypus import SimpleDocTemplate, Table, TableStyle, Paragraph, Spacer
from reportlab.lib.styles import getSampleStyleSheet


@dataclass
class TbResult:
    """Result of a single test bench (TB) run."""

    tb_id: str
    title: str
    status: str  # "PASS", "FAIL", or "SKIP"
    notes: str = ""
    duration_s: float = 0.0


@dataclass
class HilReport:
    """Collects TB results and renders a PDF report."""

    device_id: str = "KXKM-BMU-001"
    firmware_sha: str = "unknown"
    tester: str = "auto"
    results: list[TbResult] = field(default_factory=list)

    def add(self, result: TbResult) -> None:
        """Append a test result."""
        self.results.append(result)

    def render_pdf(self, out_path: Optional[str | Path] = None) -> Path:
        """Generate a colour-coded PDF report and return its path."""
        if out_path is None:
            ts = datetime.datetime.now().strftime("%Y%m%d_%H%M%S")
            out_path = Path(f"hil_report_{ts}.pdf")
        else:
            out_path = Path(out_path)

        doc = SimpleDocTemplate(
            str(out_path),
            pagesize=A4,
            leftMargin=15 * mm,
            rightMargin=15 * mm,
            topMargin=15 * mm,
            bottomMargin=15 * mm,
        )

        styles = getSampleStyleSheet()
        elements: list = []

        # Title
        elements.append(Paragraph("KXKM BMU — HIL Test Report", styles["Title"]))
        elements.append(Spacer(1, 6 * mm))

        # Metadata
        meta_lines = [
            f"<b>Date:</b> {datetime.datetime.now().strftime('%Y-%m-%d %H:%M')}",
            f"<b>Device:</b> {self.device_id}",
            f"<b>Firmware SHA:</b> {self.firmware_sha}",
            f"<b>Tester:</b> {self.tester}",
        ]
        for line in meta_lines:
            elements.append(Paragraph(line, styles["Normal"]))
        elements.append(Spacer(1, 8 * mm))

        # Summary counts
        n_pass = sum(1 for r in self.results if r.status == "PASS")
        n_fail = sum(1 for r in self.results if r.status == "FAIL")
        n_skip = sum(1 for r in self.results if r.status == "SKIP")
        elements.append(
            Paragraph(
                f"<b>Summary:</b> {n_pass} PASS / {n_fail} FAIL / {n_skip} SKIP"
                f" ({len(self.results)} total)",
                styles["Normal"],
            )
        )
        elements.append(Spacer(1, 6 * mm))

        # Results table
        header = ["TB", "Title", "Status", "Duration", "Notes"]
        data = [header]
        for r in self.results:
            dur = f"{r.duration_s:.1f}s" if r.duration_s > 0 else "-"
            data.append([r.tb_id, r.title, r.status, dur, r.notes or ""])

        table = Table(data, colWidths=[18 * mm, 50 * mm, 18 * mm, 22 * mm, 60 * mm])

        # Color-code status cells
        style_cmds = [
            ("BACKGROUND", (0, 0), (-1, 0), colors.HexColor("#334155")),
            ("TEXTCOLOR", (0, 0), (-1, 0), colors.white),
            ("FONTNAME", (0, 0), (-1, 0), "Helvetica-Bold"),
            ("FONTSIZE", (0, 0), (-1, -1), 9),
            ("GRID", (0, 0), (-1, -1), 0.5, colors.grey),
            ("VALIGN", (0, 0), (-1, -1), "MIDDLE"),
            ("TOPPADDING", (0, 0), (-1, -1), 3),
            ("BOTTOMPADDING", (0, 0), (-1, -1), 3),
        ]
        for row_idx, r in enumerate(self.results, start=1):
            if r.status == "PASS":
                bg = colors.HexColor("#bbf7d0")
            elif r.status == "FAIL":
                bg = colors.HexColor("#fecaca")
            else:
                bg = colors.HexColor("#fef08a")
            style_cmds.append(("BACKGROUND", (2, row_idx), (2, row_idx), bg))

        table.setStyle(TableStyle(style_cmds))
        elements.append(table)

        doc.build(elements)
        return out_path
