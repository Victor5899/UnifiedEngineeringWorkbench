import { useCallback, useEffect, useState } from "react";
import { Cable, RefreshCw, Unplug } from "lucide-react";
import { Field, InspectorPanel, InspectorSection, Note } from "../../components/inspector/InspectorPanel";
import { usePublishWorkspace } from "../../hooks/useWorkspace";
import {
  ApiError,
  armSimulatedHardwareFailure,
  connectEmbeddedDevice,
  disconnectEmbeddedDevice,
  fetchEmbeddedCapabilities,
  fetchEmbeddedDevices,
  fetchEmbeddedTelemetry,
} from "../../services/api";
import type { CapabilityList, EmbeddedDevice, EmbeddedDeviceList, TelemetryReport } from "../../types/hardware";

const deviceOrder = ["microcontroller", "fpga", "jtag_adapter"] as const;

function typeLabel(type: EmbeddedDevice["device_type"]): string {
  if (type === "microcontroller") return "Microcontroller";
  if (type === "fpga") return "FPGA development board";
  return "JTAG debug adapter";
}

function byDeviceOrder(devices: readonly EmbeddedDevice[]): EmbeddedDevice[] {
  return deviceOrder
    .map((type) => devices.find((device) => device.device_type === type))
    .filter((device): device is EmbeddedDevice => device !== undefined);
}

export function HardwarePage({ active }: { active: boolean }) {
  const [devices, setDevices] = useState<EmbeddedDeviceList | null>(null);
  const [capabilities, setCapabilities] = useState<CapabilityList | null>(null);
  const [telemetry, setTelemetry] = useState<Record<string, TelemetryReport>>({});
  const [selectedId, setSelectedId] = useState<string | null>(null);
  const [error, setError] = useState<string | null>(null);
  const [notice, setNotice] = useState<string | null>(null);
  const [pending, setPending] = useState(false);

  const ordered = devices ? byDeviceOrder(devices.devices) : [];
  const selected = ordered.find((device) => device.id === selectedId) ?? ordered[0] ?? null;
  const selectedTelemetry = selected ? telemetry[selected.id] : undefined;
  const jtag = ordered.find((device) => device.device_type === "jtag_adapter") ?? null;
  const phase = error ? "error" : pending ? "working" : devices ? "success" : "idle";
  const message = error
    ? error
    : pending
      ? "Waiting for the hardware API."
      : notice
        ? notice
        : devices
          ? "Simulated devices only. No physical hardware is connected."
          : "Hardware catalog has not been loaded.";
  usePublishWorkspace(active, selected?.display_name ?? null, phase, message);

  const applyCatalog = useCallback(async (list: EmbeddedDeviceList, caps: CapabilityList) => {
    const missing = list.devices.filter((device) => !caps.devices.some((entry) => entry.id === device.id));
    if (missing.length > 0) {
      throw new ApiError("The capabilities response did not include every discovered device.", 200);
    }
    const readings = await Promise.all(
      list.devices.map(async (device) => {
        if (device.connection_state !== "connected") return null;
        const report = await fetchEmbeddedTelemetry(device.id);
        return [device.id, report] as const;
      }),
    );
    const next: Record<string, TelemetryReport> = {};
    for (const entry of readings) {
      if (entry) next[entry[0]] = entry[1];
    }
    setDevices(list);
    setCapabilities(caps);
    setTelemetry(next);
    setSelectedId((current) => current ?? list.devices[0]?.id ?? null);
  }, []);

  const refresh = useCallback(async () => {
    const [list, caps] = await Promise.all([fetchEmbeddedDevices(), fetchEmbeddedCapabilities()]);
    await applyCatalog(list, caps);
  }, [applyCatalog]);

  useEffect(() => {
    if (!active) return;
    let cancelled = false;
    const load = async () => {
      setPending(true);
      try {
        const [list, caps] = await Promise.all([fetchEmbeddedDevices(), fetchEmbeddedCapabilities()]);
        if (cancelled) return;
        await applyCatalog(list, caps);
        setError(null);
      } catch (caught) {
        if (!cancelled) setError(caught instanceof ApiError ? caught.message : "Could not reach the backend.");
      } finally {
        if (!cancelled) setPending(false);
      }
    };
    void load();
    return () => {
      cancelled = true;
    };
  }, [active, applyCatalog]);

  const run = async (action: () => Promise<string>) => {
    setPending(true);
    setError(null);
    setNotice(null);
    try {
      const nextNotice = await action();
      await refresh();
      setNotice(nextNotice);
    } catch (caught) {
      setNotice(null);
      setError(caught instanceof ApiError ? caught.message : "Could not reach the backend.");
      try {
        await refresh();
      } catch {
        // The action error is already visible. A failed refresh leaves the last catalog in place.
      }
    } finally {
      setPending(false);
    }
  };

  const onConnect = (device: EmbeddedDevice) => {
    void run(async () => {
      const result = await connectEmbeddedDevice(device.id);
      return result.message;
    });
  };

  const onDisconnect = (device: EmbeddedDevice) => {
    void run(async () => {
      const result = await disconnectEmbeddedDevice(device.id);
      return result.message;
    });
  };

  const onDiscover = () => {
    void run(async () => "Discovery completed from the hardware API. All three profiles are simulated.");
  };

  const onTimeout = () => {
    if (!jtag) return;
    void run(async () => {
      if (jtag.connection_state === "connected") {
        await disconnectEmbeddedDevice(jtag.id);
      }
      const armed = await armSimulatedHardwareFailure(jtag.id, "connection_timeout");
      try {
        await connectEmbeddedDevice(jtag.id);
      } catch (caught) {
        if (caught instanceof ApiError) {
          throw new ApiError(
            `${armed.message} ${caught.message} Connect the JTAG adapter again to recover.`,
            caught.status,
          );
        }
        throw caught;
      }
      throw new ApiError("The simulated timeout did not occur. The backend accepted the JTAG connection.", 200);
    });
  };

  const capabilitySummary = (device: EmbeddedDevice) =>
    capabilities?.devices.find((entry) => entry.id === device.id)?.capabilities ?? device.capabilities;

  return (
    <div className={active ? "flex min-h-0 flex-1 flex-col overflow-hidden lg:flex-row" : "hidden"}>
      <section className="min-w-0 flex-1 overflow-y-auto p-6">
        <p className="text-xs font-semibold uppercase tracking-[0.16em] text-mist-500">Hardware integration</p>
        <h2 className="mt-2 text-2xl font-semibold text-mist-100">Simulated host adapters</h2>
        <p className="mt-3 max-w-3xl text-sm leading-6 text-mist-300">
          Three mock adapters stand in for an Arduino Uno Rev3-class microcontroller, a Digilent Arty A7-100-class
          FPGA board, and a Digilent JTAG-HS2-class debug adapter. The cards use the C++ hardware API. No USB,
          serial, JTAG, or FPGA session is opened.
        </p>

        <p className="mt-4 max-w-3xl rounded-md border border-warn/40 bg-ink-900 px-3 py-2 text-sm text-warn" role="status">
          {devices
            ? `API reports physical hardware connected: ${String(devices.physical_hardware_connected)}. Simulated catalog: ${String(devices.simulated)}.`
            : "Physical hardware is not connected. Device status appears after the API responds."}
        </p>

        {error ? (
          <p className="mt-4 max-w-3xl rounded-md border border-danger/40 bg-ink-900 px-3 py-2 text-sm text-danger" role="alert">
            {error}
          </p>
        ) : null}
        {notice ? <p className="mt-4 max-w-3xl text-sm leading-6 text-mist-300">{notice}</p> : null}

        <div className="mt-4 flex flex-wrap gap-2">
          <button type="button" className="btn btn-primary" disabled={pending} onClick={onDiscover}>
            <RefreshCw size={15} />
            Discover devices
          </button>
          <button type="button" className="btn" disabled={pending || !jtag} onClick={onTimeout}>
            <Cable size={15} />
            Simulate JTAG timeout
          </button>
        </div>

        <div className="mt-6 grid max-w-5xl gap-3 xl:grid-cols-3">
          {ordered.map((device) => {
            const connected = device.connection_state === "connected";
            const selectedCard = selected?.id === device.id;
            return (
              <article
                key={device.id}
                className={`rounded-md border bg-ink-900 p-4 ${selectedCard ? "border-accent/60" : "border-ink-600"}`}
              >
                <div className="flex items-start justify-between gap-3">
                  <div>
                    <p className="text-[11px] uppercase tracking-[0.14em] text-mist-500">{typeLabel(device.device_type)}</p>
                    <h3 className="mt-1 text-sm font-semibold text-mist-100">{device.display_name}</h3>
                  </div>
                  <span className="rounded-md border border-warn/40 px-2 py-0.5 text-[11px] text-warn">Simulated</span>
                </div>
                <dl className="mt-3 space-y-1 text-xs text-mist-300">
                  <div className="flex justify-between gap-3">
                    <dt>Availability</dt>
                    <dd>{device.available ? "Profile registered" : "Unavailable"}</dd>
                  </div>
                  <div className="flex justify-between gap-3">
                    <dt>Connection</dt>
                    <dd className={connected ? "text-accent" : "text-mist-100"}>{device.connection_state}</dd>
                  </div>
                </dl>
                <ul className="mt-3 space-y-2">
                  {capabilitySummary(device).map((capability) => (
                    <li key={capability.id} className="text-xs leading-5 text-mist-300">
                      <span className="mono text-mist-100">{capability.id}</span>
                      <span className="mt-0.5 block">{capability.summary}</span>
                    </li>
                  ))}
                </ul>
                <div className="mt-4 flex flex-wrap gap-2">
                  <button type="button" className="btn" disabled={pending} onClick={() => setSelectedId(device.id)}>
                    Inspect
                  </button>
                  {connected ? (
                    <button type="button" className="btn" disabled={pending} onClick={() => onDisconnect(device)}>
                      <Unplug size={15} />
                      Disconnect
                    </button>
                  ) : (
                    <button type="button" className="btn btn-primary" disabled={pending} onClick={() => onConnect(device)}>
                      Connect
                    </button>
                  )}
                </div>
              </article>
            );
          })}
        </div>

        <div className="mt-6 max-w-3xl space-y-3 text-sm leading-6 text-mist-300">
          <h3 className="text-sm font-semibold text-mist-100">Architecture and safety</h3>
          <p>
            The host manager keeps a separate adapter for each device. The Uno-class profile is a 5 V
            microcontroller on USB serial. The Arty-class profile is a 3.3 V FPGA board. The JTAG-HS2-class
            profile is a debug cable, not a third computer. Those domains are not tied together in this project.
          </p>
          <p>
            A 5 V output must not drive an Arty shield or Pmod pin. Digilent&apos;s manual warns against levels
            above 3.4 V. JTAG VREF has to come from the target and must share the target ground. Matching
            connectors, including a 6-pin header, do not make the Uno ICSP header and Arty J8 electrically
            interchangeable. The design record is <span className="mono text-mist-100">docs/embedded_integration.md</span>.
          </p>
          <p>
            After a simulated JTAG timeout the adapter stays disconnected and the other cards keep their own
            state. Connect that adapter again to recover. The timeout is a software flag, not a measured cable fault.
          </p>
        </div>
      </section>
      <InspectorPanel title="Device telemetry">
        <InspectorSection title="Selection">
          <Field label="Device" value={selected?.display_name ?? "None"} />
          <Field label="Type" value={selected ? typeLabel(selected.device_type) : "None"} />
          <Field label="Identifier" value={selected?.id ?? "None"} />
          <Field label="Connection" value={selected?.connection_state ?? "unavailable"} />
          <Field label="Simulated" value={selected ? "yes" : "unavailable"} />
          <Field
            label="Physical hardware"
            value={devices ? String(devices.physical_hardware_connected) : "unknown"}
          />
        </InspectorSection>
        <InspectorSection title="Telemetry">
          {selectedTelemetry ? (
            <>
              {selectedTelemetry.readings.map((reading) => (
                <Field
                  key={reading.name}
                  label={reading.name}
                  value={reading.unit ? `${reading.value} ${reading.unit}` : reading.value}
                />
              ))}
              <p className="pt-2 text-xs leading-5 text-mist-500">{selectedTelemetry.notice}</p>
            </>
          ) : (
            <p className="text-xs leading-5 text-mist-500">
              Telemetry is requested from the API only while this device is connected.
            </p>
          )}
        </InspectorSection>
        <Note>
          Reference profile: {selected?.reference_profile ?? "Load the catalog to see a profile."} Nothing on this
          page programs a chip or reads a live pin.
        </Note>
      </InspectorPanel>
    </div>
  );
}
