import type { BackpropHistoryRow } from "../../types/backpropagation";

export function LossChart({ history }: { history: readonly BackpropHistoryRow[] }) {
  const width = 680;
  const height = 300;
  const left = 62;
  const right = 18;
  const top = 18;
  const bottom = 46;
  const plotWidth = width - left - right;
  const plotHeight = height - top - bottom;
  const losses = history.map((row) => row.loss);
  const peak = Math.max(...losses, 0);
  const yMax = peak > 0 ? peak * 1.08 : 1;
  const xMin = history[0]?.step ?? 0;
  const xMax = history[history.length - 1]?.step ?? 1;
  const yOf = (value: number) => top + plotHeight - (value / yMax) * plotHeight;
  const xOf = (step: number) => {
    if (xMax === xMin) return left + plotWidth / 2;
    return left + ((step - xMin) / (xMax - xMin)) * plotWidth;
  };
  const polyline = history.map((row) => `${xOf(row.step)},${yOf(row.loss)}`).join(" ");
  const yTicks = [0, yMax / 4, yMax / 2, (3 * yMax) / 4, yMax];
  const xTicks = [0, 25, 50, 75, 100].filter((step) => step >= xMin && step <= xMax);
  const initial = history[0]?.loss;
  const final = history[history.length - 1]?.loss;
  const label =
    initial !== undefined && final !== undefined
      ? `Training loss from step ${xMin} to ${xMax}. Initial loss ${initial}, final loss ${final}.`
      : "Training loss";

  return (
    <figure className="rounded-md border border-ink-600 bg-ink-900 p-4">
      <figcaption className="text-sm font-semibold text-mist-100">Training loss</figcaption>
      <p className="mt-1 text-xs text-mist-500">Loss after each simultaneous gradient step. Step 0 is the initial loss.</p>
      <svg viewBox={`0 0 ${width} ${height}`} role="img" aria-label={label} className="mt-3 w-full">
        {yTicks.map((tick) => (
          <g key={tick}>
            <line x1={left} x2={width - right} y1={yOf(tick)} y2={yOf(tick)} stroke="#313c4e" strokeWidth="1" />
            <text x={left - 8} y={yOf(tick) + 4} textAnchor="end" fill="#7f8da3" fontSize="11">
              {formatTick(tick)}
            </text>
          </g>
        ))}
        {xTicks.map((step) => (
          <g key={step}>
            <line x1={xOf(step)} x2={xOf(step)} y1={top} y2={top + plotHeight} stroke="#313c4e" strokeWidth="1" />
            <text x={xOf(step)} y={height - 22} textAnchor="middle" fill="#b7c3d4" fontSize="11">
              {step}
            </text>
          </g>
        ))}
        <line x1={left} x2={left} y1={top} y2={top + plotHeight} stroke="#7f8da3" strokeWidth="1" />
        <line x1={left} x2={width - right} y1={top + plotHeight} y2={top + plotHeight} stroke="#7f8da3" strokeWidth="1" />
        <polyline fill="none" stroke="#3ecfb2" strokeWidth="2" points={polyline} />
        <text x={width / 2} y={height - 6} textAnchor="middle" fill="#b7c3d4" fontSize="12">
          Training step
        </text>
        <text transform={`translate(16 ${top + plotHeight / 2}) rotate(-90)`} textAnchor="middle" fill="#b7c3d4" fontSize="12">
          Loss
        </text>
      </svg>
    </figure>
  );
}

function formatTick(value: number): string {
  if (value === 0) return "0";
  if (Math.abs(value) < 0.001 || Math.abs(value) >= 1000) return value.toExponential(1);
  const text = value.toPrecision(3);
  return text.replace(/(\.\d*?)0+$/, "$1").replace(/\.$/, "");
}
