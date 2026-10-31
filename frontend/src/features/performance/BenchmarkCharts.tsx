export interface ChartPoint {
  label: string;
  value: number;
}

export function BenchmarkChart({
  title,
  yLabel,
  points,
  reference,
}: {
  title: string;
  yLabel: string;
  points: readonly ChartPoint[];
  reference?: number;
}) {
  const width = 420;
  const height = 210;
  const left = 52;
  const right = 16;
  const top = 16;
  const bottom = 36;
  const plotWidth = width - left - right;
  const plotHeight = height - top - bottom;
  const peak = Math.max(reference ?? 0, ...points.map((point) => point.value), 1e-12);
  const yMax = peak * 1.15;
  const yOf = (value: number) => top + plotHeight - (value / yMax) * plotHeight;
  const xOf = (index: number) =>
    points.length <= 1 ? left + plotWidth / 2 : left + (index * plotWidth) / (points.length - 1);
  const polyline = points.map((point, index) => `${xOf(index)},${yOf(point.value)}`).join(" ");
  const ticks = [0, yMax / 2, yMax];

  return (
    <figure className="rounded-md border border-ink-600 bg-ink-900 p-4">
      <figcaption className="text-sm font-semibold text-mist-100">{title}</figcaption>
      <p className="mt-1 text-[11px] uppercase tracking-[0.14em] text-mist-500">{yLabel}</p>
      <svg viewBox={`0 0 ${width} ${height}`} role="img" aria-label={title} className="mt-2 w-full">
        {ticks.map((tick) => (
          <g key={tick}>
            <line
              x1={left}
              x2={width - right}
              y1={yOf(tick)}
              y2={yOf(tick)}
              stroke="#313c4e"
              strokeWidth="1"
            />
            <text x={left - 8} y={yOf(tick) + 4} textAnchor="end" fill="#7f8da3" fontSize="10">
              {formatTick(tick)}
            </text>
          </g>
        ))}
        {reference !== undefined ? (
          <line
            x1={left}
            x2={width - right}
            y1={yOf(reference)}
            y2={yOf(reference)}
            stroke="#e2a04a"
            strokeDasharray="4 3"
            strokeWidth="1"
          />
        ) : null}
        <polyline fill="none" stroke="#3ecfb2" strokeWidth="2" points={polyline} />
        {points.map((point, index) => (
          <g key={point.label}>
            <circle cx={xOf(index)} cy={yOf(point.value)} r="3.5" fill="#3ecfb2" />
            <text x={xOf(index)} y={height - 14} textAnchor="middle" fill="#b7c3d4" fontSize="11">
              {point.label}
            </text>
          </g>
        ))}
      </svg>
    </figure>
  );
}

function formatTick(value: number): string {
  if (value === 0) return "0";
  if (value < 0.001) return value.toExponential(1);
  if (value >= 10) return value.toFixed(1);
  return value.toFixed(3);
}
