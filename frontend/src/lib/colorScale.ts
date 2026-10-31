const STOPS: ReadonlyArray<{ t: number; rgb: readonly [number, number, number] }> = [
  { t: 0, rgb: [37, 99, 235] },
  { t: 0.25, rgb: [14, 165, 233] },
  { t: 0.5, rgb: [45, 212, 191] },
  { t: 0.75, rgb: [250, 204, 21] },
  { t: 1, rgb: [239, 68, 68] },
];

function clamp01(value: number): number {
  if (value < 0) return 0;
  if (value > 1) return 1;
  return value;
}

export function temperatureUnit(temperature: number, minimum: number, maximum: number): number {
  const span = maximum - minimum;
  if (!(span > 0)) return 0.5;
  return clamp01((temperature - minimum) / span);
}

export function temperatureRgb(
  temperature: number,
  minimum: number,
  maximum: number,
): [number, number, number] {
  const unit = temperatureUnit(temperature, minimum, maximum);
  let lower = STOPS[0];
  let upper = STOPS[STOPS.length - 1];
  for (let index = 1; index < STOPS.length; index += 1) {
    if (unit <= STOPS[index].t) {
      lower = STOPS[index - 1];
      upper = STOPS[index];
      break;
    }
  }
  const width = upper.t - lower.t;
  const local = width === 0 ? 0 : (unit - lower.t) / width;
  return [
    Math.round(lower.rgb[0] + (upper.rgb[0] - lower.rgb[0]) * local),
    Math.round(lower.rgb[1] + (upper.rgb[1] - lower.rgb[1]) * local),
    Math.round(lower.rgb[2] + (upper.rgb[2] - lower.rgb[2]) * local),
  ];
}

export function temperatureCss(temperature: number, minimum: number, maximum: number): string {
  const [red, green, blue] = temperatureRgb(temperature, minimum, maximum);
  return `rgb(${red}, ${green}, ${blue})`;
}

export function temperatureGradientCss(): string {
  const stops = STOPS.map(
    (stop) => `rgb(${stop.rgb[0]}, ${stop.rgb[1]}, ${stop.rgb[2]}) ${stop.t * 100}%`,
  );
  return `linear-gradient(to top, ${stops.join(", ")})`;
}
