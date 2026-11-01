import { useEffect, useState } from "react";
import { Download } from "lucide-react";
import { Field, InspectorPanel, InspectorSection, Note } from "../../components/inspector/InspectorPanel";
import { usePublishWorkspace } from "../../hooks/useWorkspace";
import { ApiError, downloadBackpropCsv, fetchBackpropagation } from "../../services/api";
import type { BackpropParameters, BackpropReport } from "../../types/backpropagation";
import { LossChart } from "./LossChart";

export function BackpropPage({ active }: { active: boolean }) {
  const [report, setReport] = useState<BackpropReport | null>(null);
  const [error, setError] = useState<string | null>(null);
  const [loading, setLoading] = useState(false);

  const phase = error ? "error" : report ? (report.gradient_check_passed ? "success" : "error") : "working";
  const message = error
    ? error
    : report
      ? report.gradient_check_passed
        ? "Gradient check passed."
        : "Gradient check failed."
      : "Loading the backpropagation result.";
  usePublishWorkspace(active, report ? "x = 1, y = 2" : null, phase, message);

  useEffect(() => {
    if (!active) return;
    let cancelled = false;
    setLoading(true);
    fetchBackpropagation()
      .then((next) => {
        if (cancelled) return;
        setReport(next);
        setError(null);
      })
      .catch((caught: unknown) => {
        if (cancelled) return;
        setReport(null);
        setError(caught instanceof ApiError ? caught.message : "Could not reach the backend.");
      })
      .finally(() => {
        if (!cancelled) setLoading(false);
      });
    return () => {
      cancelled = true;
    };
  }, [active]);

  const onExport = async () => {
    try {
      const file = await downloadBackpropCsv();
      const blob = new Blob([file.csv], { type: "text/csv;charset=utf-8" });
      const url = URL.createObjectURL(blob);
      const link = document.createElement("a");
      link.href = url;
      link.download = file.filename;
      link.click();
      URL.revokeObjectURL(url);
    } catch (caught: unknown) {
      setError(caught instanceof ApiError ? caught.message : "Could not download the CSV.");
    }
  };

  return (
    <div className={active ? "flex min-h-0 flex-1 flex-col overflow-hidden lg:flex-row" : "hidden"}>
      <section className="min-w-0 flex-1 overflow-y-auto p-6">
        <p className="text-xs font-semibold uppercase tracking-[0.16em] text-mist-500">Task F</p>
        <h2 className="mt-2 text-2xl font-semibold text-mist-100">Backpropagation</h2>
        <p className="mt-3 max-w-3xl text-sm leading-6 text-mist-300">
          One sample passes through one tanh hidden neuron. The C++ engine evaluates the forward pass, the
          analytical gradients, a central-difference check, and 100 simultaneous gradient steps. This page
          displays that result. It does not recompute the derivatives.
        </p>
        <pre className="mono mt-4 max-w-3xl overflow-x-auto rounded-md border border-ink-600 bg-ink-900 p-4 text-sm leading-6 text-mist-100">
          {`h = tanh(w*x + b)\ny_hat = v*h + c\nJ = 0.5*(y_hat - y)^2`}
        </pre>

        {error ? (
          <p className="mt-4 max-w-3xl rounded-md border border-danger/40 bg-ink-900 px-3 py-2 text-sm text-danger" role="alert">
            {error}
          </p>
        ) : null}
        {loading && !report ? <p className="mt-4 text-sm text-mist-300">Reading the result from the C++ backend.</p> : null}

        {report ? (
          <>
            <div className="mt-6 flex flex-wrap items-center gap-3">
              <p
                className={`rounded-md border px-3 py-1.5 text-sm font-semibold ${
                  report.gradient_check_passed
                    ? "border-ok/40 bg-ink-900 text-ok"
                    : "border-danger/40 bg-ink-900 text-danger"
                }`}
              >
                Gradient check {report.gradient_check_passed ? "PASS" : "FAIL"}
              </p>
              <button className="btn" type="button" onClick={() => void onExport()}>
                <Download size={16} />
                Download CSV
              </button>
            </div>

            <h3 className="mt-8 text-sm font-semibold text-mist-100">Initial parameters</h3>
            <ParameterGrid parameters={report.initial_parameters} />

            <h3 className="mt-8 text-sm font-semibold text-mist-100">Initial forward pass</h3>
            <div className="mt-3 grid max-w-4xl gap-3 sm:grid-cols-3">
              <Metric label="Hidden activation h" value={report.initial_forward.h} />
              <Metric label="Prediction y_hat" value={report.initial_forward.y_hat} />
              <Metric label="Loss" value={report.initial_forward.loss} />
            </div>

            <h3 className="mt-8 text-sm font-semibold text-mist-100">Analytical and numerical gradients</h3>
            <p className="mt-2 max-w-3xl text-sm leading-6 text-mist-300">
              Each absolute error must be at most {formatValue(report.gradient_tolerance)}. The numerical column is a
              central difference with epsilon {formatValue(report.epsilon)}. Both columns come from the initial
              parameters.
            </p>
            <div className="mt-3 max-w-5xl overflow-x-auto rounded-md border border-ink-600">
              <table className="w-full min-w-[44rem] text-left text-sm">
                <thead className="bg-ink-900 text-[11px] uppercase tracking-[0.14em] text-mist-500">
                  <tr>
                    <th className="px-3 py-2 font-semibold">Parameter</th>
                    <th className="px-3 py-2 font-semibold">Analytical gradient</th>
                    <th className="px-3 py-2 font-semibold">Numerical gradient</th>
                    <th className="px-3 py-2 font-semibold">Absolute error</th>
                    <th className="px-3 py-2 font-semibold">Result</th>
                  </tr>
                </thead>
                <tbody>
                  {report.gradient_checks.map((check) => (
                    <tr key={check.parameter} className="border-t border-ink-700">
                      <td className="mono px-3 py-2 text-mist-100">{check.parameter}</td>
                      <td className="mono px-3 py-2 text-mist-100">{formatValue(check.analytical_gradient)}</td>
                      <td className="mono px-3 py-2 text-mist-100">{formatValue(check.numerical_gradient)}</td>
                      <td className="mono px-3 py-2 text-mist-100">{formatValue(check.absolute_error)}</td>
                      <td className={`px-3 py-2 font-semibold ${check.passed ? "text-ok" : "text-danger"}`}>
                        {check.passed ? "PASS" : "FAIL"}
                      </td>
                    </tr>
                  ))}
                </tbody>
              </table>
            </div>

            <h3 className="mt-8 text-sm font-semibold text-mist-100">Loss versus training step</h3>
            <div className="mt-3 max-w-5xl">
              <LossChart history={report.history} />
            </div>

            <h3 className="mt-8 text-sm font-semibold text-mist-100">Parameters after 100 updates</h3>
            <ParameterGrid parameters={report.final_parameters} />
            <p className="mt-3 text-sm text-mist-300">
              Final loss <span className="mono text-mist-100">{formatValue(report.final_loss)}</span>
            </p>
          </>
        ) : null}
      </section>
      <InspectorPanel title="Experiment">
        {report ? (
          <>
            <InspectorSection title="Model">
              <Field label="Samples" value={String(report.training_samples)} />
              <Field label="Hidden neurons" value={String(report.hidden_neurons)} />
              <Field label="Activation" value={report.activation} />
              <Field label="Loss" value={report.loss_definition} />
            </InspectorSection>
            <InspectorSection title="Sample">
              <Field label="x" value={formatValue(report.sample.x)} />
              <Field label="y" value={formatValue(report.sample.y)} />
              <Field label="Learning rate" value={formatValue(report.learning_rate)} />
              <Field label="Epsilon" value={formatValue(report.epsilon)} />
            </InspectorSection>
            <InspectorSection title="Forward pass">
              <Field label="z" value={formatValue(report.initial_forward.z)} />
              <Field label="Residual e" value={formatValue(report.initial_forward.error)} />
              <Field label="Initial loss" value={formatValue(report.initial_loss)} />
              <Field label="Final loss" value={formatValue(report.final_loss)} />
            </InspectorSection>
            <InspectorSection title="After one update">
              <Field label="w" value={formatValue(report.parameters_after_one_update.w)} />
              <Field label="b" value={formatValue(report.parameters_after_one_update.b)} />
              <Field label="v" value={formatValue(report.parameters_after_one_update.v)} />
              <Field label="c" value={formatValue(report.parameters_after_one_update.c)} />
            </InspectorSection>
            <Note>
              The gradient check compares each analytical derivative with a central difference at the initial
              parameters. Training then takes 100 steps from those same parameters. The saved figure is redrawn from
              the CSV by scripts/plot_backpropagation_loss.py.
            </Note>
          </>
        ) : (
          <Note>{loading ? "Waiting for the C++ result." : "Start the backend to load this experiment."}</Note>
        )}
      </InspectorPanel>
    </div>
  );
}

function ParameterGrid({ parameters }: { parameters: BackpropParameters }) {
  return (
    <div className="mt-3 grid max-w-4xl gap-3 sm:grid-cols-4">
      <Metric label="w" value={parameters.w} />
      <Metric label="b" value={parameters.b} />
      <Metric label="v" value={parameters.v} />
      <Metric label="c" value={parameters.c} />
    </div>
  );
}

function Metric({ label, value }: { label: string; value: number }) {
  return (
    <article className="rounded-md border border-ink-600 bg-ink-900 px-3 py-3">
      <p className="text-[11px] uppercase tracking-[0.14em] text-mist-500">{label}</p>
      <p className="mono mt-1 text-sm text-mist-100">{formatValue(value)}</p>
    </article>
  );
}

function formatValue(value: number): string {
  if (!Number.isFinite(value)) return "n/a";
  const magnitude = Math.abs(value);
  if (magnitude !== 0 && magnitude < 1e-4) return value.toExponential(4);
  const text = value.toPrecision(12);
  if (/[eE]/.test(text)) return text;
  return text.replace(/(\.\d*?[1-9])0+$/, "$1").replace(/\.0+$/, "");
}
