export interface BackpropParameters {
  w: number;
  b: number;
  v: number;
  c: number;
}

export interface BackpropForward {
  z: number;
  h: number;
  y_hat: number;
  error: number;
  loss: number;
}

export interface BackpropGradients {
  dJ_dw: number;
  dJ_db: number;
  dJ_dv: number;
  dJ_dc: number;
}

export interface BackpropGradientCheck {
  parameter: "w" | "b" | "v" | "c";
  analytical_gradient: number;
  numerical_gradient: number;
  absolute_error: number;
  passed: boolean;
}

export interface BackpropHistoryRow extends BackpropParameters {
  step: number;
  loss: number;
}

export interface BackpropReport {
  training_samples: number;
  hidden_neurons: number;
  activation: string;
  loss_definition: string;
  sample: { x: number; y: number };
  learning_rate: number;
  epsilon: number;
  gradient_tolerance: number;
  training_steps: number;
  initial_parameters: BackpropParameters;
  initial_forward: BackpropForward;
  analytical_gradients: BackpropGradients;
  gradient_checks: BackpropGradientCheck[];
  gradient_check_passed: boolean;
  parameters_after_one_update: BackpropParameters;
  initial_loss: number;
  final_loss: number;
  final_parameters: BackpropParameters;
  history: BackpropHistoryRow[];
}
