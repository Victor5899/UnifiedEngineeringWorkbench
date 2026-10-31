export interface HardwareInfo {
  cpu_model: string | null;
  physical_cores: number | null;
  logical_cpus: number | null;
  ram_bytes: number | null;
  operating_system: string | null;
  compiler: string | null;
  compiler_version: string | null;
  build_type: string | null;
  optimization_flags: string | null;
  maximum_worker_count: number;
}

export interface BenchmarkConfiguration {
  execution_mode: string;
  worker_count: number;
  warmup_time_seconds: number;
  execution_time_seconds: number[];
  median_execution_time_seconds: number;
  speedup: number;
  parallel_efficiency: number;
  maximum_absolute_error_celsius: number;
  numerical_validation: string;
}

export interface BenchmarkResult {
  benchmark_id: string;
  timestamp_utc: string;
  random_seed: number;
  batch_size: number;
  mesh_size: number;
  repetitions: number;
  workload_generation_time_seconds: number;
  warmup_time_seconds: number;
  total_duration_seconds: number;
  speedup_reference: string;
  numerical_tolerance_celsius: number;
  maximum_absolute_error_celsius: number;
  numerical_validation: string;
  hardware: HardwareInfo;
  configurations: BenchmarkConfiguration[];
}

export interface BenchmarkStatus {
  state: string;
  benchmark_id: string | null;
  phase: string;
  message: string;
  worker_count: number;
  repetition: number;
  repetitions: number;
  cases_completed: number;
  cases_total: number;
  completed_steps: number;
  total_steps: number;
  error: string | null;
}

export interface BenchmarkRequest {
  seed: number;
  batch_size: number;
  mesh_size: number;
  repetitions: number;
  worker_counts: number[];
}

export interface BenchmarkAccepted {
  state: string;
  benchmark_id: string;
}
